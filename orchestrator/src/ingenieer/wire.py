"""CAD bridge response wrapper — mirrors TOTaLi REPLResult contract shape."""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, ConfigDict, Field

from ingenieer.contracts import SCHEMA_VERSION, build_contract_payload


class BridgeExecutionResult(BaseModel):
    """Result from the in-process CAD add-in / bridge after executing an intent."""

    model_config = ConfigDict(extra="forbid")

    schemaVersion: str = Field(default=SCHEMA_VERSION)
    success: bool
    stdout: str = ""
    error_traceback: str | None = None
    telemetry: dict[str, Any] = Field(default_factory=dict)
    invariants: list[str] = Field(
        default_factory=lambda: [
            "schema_version_required",
            "deterministic_key_order",
            "intent_schema_validated",
            "atomic_rollback_on_failure",
        ]
    )

    def as_contract(self) -> dict[str, Any]:
        return build_contract_payload(
            artifact_type="icad_bridge_execution_result",
            invariants=self.invariants,
            metadata={"success": self.success},
            data={
                "success": self.success,
                "stdout": self.stdout,
                "errorTraceback": self.error_traceback or "",
                "telemetry": self.telemetry,
            },
        )
