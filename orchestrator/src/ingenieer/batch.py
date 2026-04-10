"""Batch pipeline: run a sequence of intents with fingerprint threading."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from pydantic import BaseModel, Field, field_validator

from ingenieer.audit import AuditLogger
from ingenieer.bridge_client import BridgeClient, create_bridge_client
from ingenieer.models import (
    CadIntentEnvelope,
    OrchestratorConfig,
    PipelineResult,
    ProjectConfig,
)
from ingenieer.orchestrator import PipelineOrchestrator


class ProjectContract(BaseModel):
    """Project-level envelope: metadata + ordered list of intents to execute."""

    project: ProjectConfig = Field(default_factory=ProjectConfig)
    intents: list[CadIntentEnvelope] = Field(min_length=1)
    execution_mode_override: str | None = Field(
        default=None,
        description="If set, override executionMode on all intents (dry_run, preview).",
    )
    human_confirmation_token: str | None = Field(
        default=None,
        description="If set, apply to all high-risk intents that lack one.",
    )

    @field_validator("intents")
    @classmethod
    def _validate_unique_intent_ids(cls, v: list[CadIntentEnvelope]) -> list[CadIntentEnvelope]:
        ids = [i.intentId for i in v]
        dupes = [x for x in ids if ids.count(x) > 1]
        if dupes:
            raise ValueError(f"duplicate intentIds in batch: {set(dupes)}")
        return v


class StepResult(BaseModel):
    """Result of one intent within a batch."""

    step_index: int
    intent_id: str
    command: str
    pipeline_result: PipelineResult
    fingerprint_before: str | None = None
    fingerprint_after: str | None = None


class BatchResult(BaseModel):
    """Aggregate result of a batch pipeline run."""

    project_id: str
    success: bool = True
    completed_steps: int = 0
    total_steps: int = 0
    step_results: list[StepResult] = Field(default_factory=list)
    last_good_fingerprint: str | None = None
    errors: list[str] = Field(default_factory=list)


class BatchPipeline:
    """Run a ProjectContract: sequential intents with fingerprint threading."""

    def __init__(
        self,
        config: OrchestratorConfig,
        audit: AuditLogger,
        output_dir: Path,
        *,
        bridge: BridgeClient | None = None,
    ) -> None:
        self._config = config
        self._audit = audit
        self._output_dir = Path(output_dir)
        self._bridge = bridge

    def run(self, contract: ProjectContract) -> BatchResult:
        config = self._config.model_copy(update={"project": contract.project})
        bridge = self._bridge or create_bridge_client(config)
        orch = PipelineOrchestrator(config, self._audit, self._output_dir, bridge=bridge)

        result = BatchResult(
            project_id=contract.project.name,
            total_steps=len(contract.intents),
        )
        current_fingerprint: str | None = None

        for idx, intent in enumerate(contract.intents):
            updates: dict[str, Any] = {}
            if contract.execution_mode_override:
                updates["executionMode"] = contract.execution_mode_override
            if contract.human_confirmation_token and not intent.humanConfirmationToken:
                updates["humanConfirmationToken"] = contract.human_confirmation_token
            if current_fingerprint:
                updates["modelFingerprintExpected"] = current_fingerprint
            if updates:
                intent = intent.model_copy(update=updates)

            self._audit.log("batch_step_start", {
                "step": idx,
                "intentId": intent.intentId,
                "command": intent.command,
            })

            pr = orch.run(intent)

            fp_after: str | None = None
            for phase in pr.phases:
                if phase.phase == "dispatch_execute" and phase.success:
                    be = phase.data.get("bridge_execution", {})
                    tel = be.get("telemetry", {})
                    fp_after = tel.get("modelFingerprintAfter")

            step = StepResult(
                step_index=idx,
                intent_id=intent.intentId,
                command=intent.command,
                pipeline_result=pr,
                fingerprint_before=current_fingerprint,
                fingerprint_after=fp_after,
            )
            result.step_results.append(step)

            if pr.success:
                result.completed_steps += 1
                if fp_after:
                    current_fingerprint = fp_after
                    result.last_good_fingerprint = fp_after
                self._audit.log("batch_step_complete", {
                    "step": idx,
                    "intentId": intent.intentId,
                    "fingerprint_after": fp_after,
                })
            else:
                result.success = False
                result.errors.extend(pr.errors)
                self._audit.log("batch_step_failed", {
                    "step": idx,
                    "intentId": intent.intentId,
                    "errors": pr.errors,
                })
                break

        self._audit.log("batch_complete", {
            "success": result.success,
            "completed": result.completed_steps,
            "total": result.total_steps,
            "last_good_fingerprint": result.last_good_fingerprint,
        })
        return result
