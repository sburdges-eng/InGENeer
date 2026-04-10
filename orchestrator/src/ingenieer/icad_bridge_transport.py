"""
iCAD bridge wire path: baseline fingerprint (GET) and intent dispatch (POST).

Implements the orchestrator side of docs/BRIDGE_TRANSPORT.md. Phases call these
helpers so sync_baseline / dispatch_execute stay aligned with the HTTP contract.
"""

from __future__ import annotations

from typing import Any

from ingenieer.bridge_client import BridgeClient
from ingenieer.models import OrchestratorContext, PhaseResult


def run_sync_baseline(bridge: BridgeClient, ctx: OrchestratorContext) -> PhaseResult:
    """GET /v1/model-fingerprint via bridge; optional stale guard vs envelope."""
    try:
        observed = bridge.get_model_fingerprint()
    except Exception as exc:  # noqa: BLE001 — surface as phase failure
        return PhaseResult(
            phase="sync_baseline",
            success=False,
            message=f"Baseline sync failed: {exc}",
            data={},
        )
    ctx.model_fingerprint_observed = observed
    intent = ctx.intent
    if intent and intent.modelFingerprintExpected:
        if observed != intent.modelFingerprintExpected:
            return PhaseResult(
                phase="sync_baseline",
                success=False,
                message="Model fingerprint mismatch (stale document)",
                data={
                    "expected": intent.modelFingerprintExpected,
                    "observed": observed,
                },
            )
    return PhaseResult(
        phase="sync_baseline",
        success=True,
        message="Baseline sync OK",
        data={"modelFingerprintObserved": observed},
    )


def run_dispatch_execute(bridge: BridgeClient, ctx: OrchestratorContext) -> PhaseResult:
    """POST /v1/execute via bridge; persist BridgeExecutionResult on context."""
    intent = ctx.intent
    if intent is None:
        return PhaseResult(
            phase="dispatch_execute",
            success=False,
            message="Dispatch exception: intent missing",
            data={},
        )
    try:
        br = bridge.execute_intent(intent)
    except Exception as exc:  # noqa: BLE001
        return PhaseResult(
            phase="dispatch_execute",
            success=False,
            message=f"Dispatch exception: {exc}",
            data={},
        )
    payload = br.model_dump(mode="json")
    ctx.bridge_execution = payload
    ack: dict[str, Any] = {
        "status": "executed" if br.success else "failed",
        "command": intent.command,
    }
    if not br.success:
        return PhaseResult(
            phase="dispatch_execute",
            success=False,
            message=br.error_traceback or "bridge reported execution failure",
            data={"dispatch_ack": ack, "bridge_execution": payload},
        )
    return PhaseResult(
        phase="dispatch_execute",
        success=True,
        message="Dispatch completed",
        data={"dispatch_ack": ack, "bridge_execution": payload},
    )
