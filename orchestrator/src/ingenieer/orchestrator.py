"""
Phase orchestrator for InGENeer: validate → baseline → dispatch → verify.

Bridge: `BridgeConfig.mode` `mock` (default) or `http` — see docs/BRIDGE_TRANSPORT.md.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Any, Protocol

from ingenieer.audit import AuditLogger
from ingenieer.bridge_client import BridgeClient, create_bridge_client
from ingenieer.intent_validation import collect_intent_validation_errors
from ingenieer.models import (
    CadIntentEnvelope,
    OrchestratorConfig,
    OrchestratorContext,
    PhaseResult,
    PipelineResult,
)


PHASE_ORDER = ("validate_intent", "sync_baseline", "dispatch_execute", "verify_result")


class PhaseProcessor(Protocol):
    name: str

    def validate_inputs(self, ctx: OrchestratorContext) -> tuple[bool, list[str]]: ...

    def run(self, ctx: OrchestratorContext) -> PhaseResult: ...


class _ValidateIntentPhase:
    def __init__(self, config: OrchestratorConfig) -> None:
        self._config = config
        self.name = "validate_intent"

    def validate_inputs(self, ctx: OrchestratorContext) -> tuple[bool, list[str]]:
        if ctx.intent is None:
            return False, ["intent is required on context"]
        return True, []

    def run(self, ctx: OrchestratorContext) -> PhaseResult:
        assert ctx.intent is not None
        errors = collect_intent_validation_errors(ctx.intent, self._config.intent_validation)
        if errors:
            return PhaseResult(
                phase=self.name,
                success=False,
                message="Intent validation failed: " + "; ".join(errors),
                data={"errors": errors},
            )
        return PhaseResult(
            phase=self.name,
            success=True,
            message="Intent envelope validated",
            data={"command": ctx.intent.command, "intentId": ctx.intent.intentId},
        )


class _SyncBaselinePhase:
    """Compare orchestrator model fingerprint with live CAD session (via bridge)."""

    def __init__(self, bridge: BridgeClient) -> None:
        self._bridge = bridge
        self.name = "sync_baseline"

    def validate_inputs(self, ctx: OrchestratorContext) -> tuple[bool, list[str]]:
        return True, []

    def run(self, ctx: OrchestratorContext) -> PhaseResult:
        try:
            observed = self._bridge.get_model_fingerprint()
        except Exception as exc:  # noqa: BLE001 — surface as phase failure
            return PhaseResult(
                phase=self.name,
                success=False,
                message=f"Baseline sync failed: {exc}",
                data={},
            )
        ctx.model_fingerprint_observed = observed
        if ctx.intent and ctx.intent.modelFingerprintExpected:
            if observed != ctx.intent.modelFingerprintExpected:
                return PhaseResult(
                    phase=self.name,
                    success=False,
                    message="Model fingerprint mismatch (stale document)",
                    data={
                        "expected": ctx.intent.modelFingerprintExpected,
                        "observed": observed,
                    },
                )
        return PhaseResult(
            phase=self.name,
            success=True,
            message="Baseline sync OK",
            data={"modelFingerprintObserved": observed},
        )


class _DispatchExecutePhase:
    def __init__(self, bridge: BridgeClient) -> None:
        self._bridge = bridge
        self.name = "dispatch_execute"

    def validate_inputs(self, ctx: OrchestratorContext) -> tuple[bool, list[str]]:
        if ctx.intent is None:
            return False, ["intent missing"]
        return True, []

    def run(self, ctx: OrchestratorContext) -> PhaseResult:
        assert ctx.intent is not None
        try:
            br = self._bridge.execute_intent(ctx.intent)
        except Exception as exc:  # noqa: BLE001
            return PhaseResult(
                phase=self.name,
                success=False,
                message=f"Dispatch exception: {exc}",
                data={},
            )
        payload = br.model_dump(mode="json")
        ctx.bridge_execution = payload
        ack: dict[str, Any] = {
            "status": "executed" if br.success else "failed",
            "command": ctx.intent.command,
        }
        if not br.success:
            return PhaseResult(
                phase=self.name,
                success=False,
                message=br.error_traceback or "bridge reported execution failure",
                data={"dispatch_ack": ack, "bridge_execution": payload},
            )
        return PhaseResult(
            phase=self.name,
            success=True,
            message="Dispatch completed",
            data={"dispatch_ack": ack, "bridge_execution": payload},
        )


class _VerifyResultPhase:
    name = "verify_result"

    def validate_inputs(self, ctx: OrchestratorContext) -> tuple[bool, list[str]]:
        return True, []

    def run(self, ctx: OrchestratorContext) -> PhaseResult:
        be = ctx.bridge_execution
        if not be:
            return PhaseResult(
                phase=self.name,
                success=False,
                message="Missing bridge execution result (dispatch did not populate context)",
                data={},
            )
        if not be.get("success"):
            return PhaseResult(
                phase=self.name,
                success=False,
                message=be.get("error_traceback") or "verification failed (bridge success=false)",
                data={"verification": {"status": "failed", "source": "bridge_execution"}},
            )
        return PhaseResult(
            phase=self.name,
            success=True,
            message="Verification OK",
            data={"verification": {"status": "ok", "source": "bridge_execution"}},
        )


class PipelineOrchestrator:
    def __init__(
        self,
        config: dict[str, Any] | OrchestratorConfig,
        audit: AuditLogger,
        output_dir: Path,
        *,
        bridge: BridgeClient | None = None,
    ) -> None:
        self.config = (
            config if isinstance(config, OrchestratorConfig) else OrchestratorConfig.model_validate(config)
        )
        self.audit = audit
        self.output_dir = Path(output_dir)
        resolved_bridge = bridge if bridge is not None else create_bridge_client(self.config)
        cfg = self.config
        self._phases: dict[str, PhaseProcessor] = {
            "validate_intent": _ValidateIntentPhase(cfg),
            "sync_baseline": _SyncBaselinePhase(resolved_bridge),
            "dispatch_execute": _DispatchExecutePhase(resolved_bridge),
            "verify_result": _VerifyResultPhase(),
        }

    def run(self, intent: CadIntentEnvelope, *, phase: str = "all") -> PipelineResult:
        t0 = time.time()
        result = PipelineResult(project_id=self.config.project.name)
        ctx = OrchestratorContext(output_dir=self.output_dir, intent=intent)

        if phase == "all":
            phases_to_run = list(PHASE_ORDER)
        else:
            if phase not in PHASE_ORDER:
                raise ValueError(f"Unknown phase {phase!r}; expected one of {PHASE_ORDER}")
            idx = PHASE_ORDER.index(phase)
            phases_to_run = list(PHASE_ORDER[: idx + 1])

        for phase_name in phases_to_run:
            processor = self._phases[phase_name]
            self.audit.log("phase_start", {"phase": phase_name})

            pt0 = time.time()
            try:
                valid, errors = processor.validate_inputs(ctx)
                if not valid:
                    pr = PhaseResult(
                        phase=phase_name,
                        success=False,
                        duration_sec=time.time() - pt0,
                        message=f"Input validation failed: {errors}",
                    )
                    result.phases.append(pr)
                    result.success = False
                    ctx.phase_status[phase_name] = "failed_validation"
                    ctx.errors.extend(errors)
                    self.audit.log("phase_failed", {"phase": phase_name, "message": pr.message})
                    break

                pr = processor.run(ctx)
                pr.duration_sec = time.time() - pt0

                if not pr.success:
                    self.audit.log("phase_failed", {"phase": phase_name, "message": pr.message})
                    result.success = False
                    result.phases.append(pr)
                    ctx.phase_status[phase_name] = "failed"
                    ctx.errors.append(pr.message)
                    break

                ctx.merge_data(pr.data)
                ctx.phase_status[phase_name] = "success"
                result.phases.append(pr)
                result.output_files.extend(pr.output_files)
                self.audit.log(
                    "phase_complete",
                    {
                        "phase": phase_name,
                        "duration_sec": pr.duration_sec,
                        "outputs": [str(f) for f in pr.output_files],
                    },
                )

            except Exception as exc:  # noqa: BLE001 — boundary: log and stop pipeline
                pr = PhaseResult(
                    phase=phase_name,
                    success=False,
                    duration_sec=time.time() - pt0,
                    message=f"Exception: {exc}",
                )
                result.phases.append(pr)
                result.success = False
                ctx.phase_status[phase_name] = "exception"
                ctx.errors.append(str(exc))
                self.audit.log("phase_failed", {"phase": phase_name, "message": pr.message})
                break

        result.errors.extend(ctx.errors)
        self.audit.log(
            "pipeline_complete",
            {
                "success": result.success,
                "duration_sec": time.time() - t0,
                "phases": [p.phase for p in result.phases],
            },
        )
        return result
