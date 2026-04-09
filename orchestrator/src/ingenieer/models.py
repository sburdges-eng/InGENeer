"""Pydantic models for CAD intent envelopes and pipeline results."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

INTENT_SCHEMA_VERSION = "1.0.0"


class ProjectConfig(BaseModel):
    name: str = "unknown"
    version: str = "0.1.0"


class AuditConfig(BaseModel):
    log_dir: str = "audit_logs"
    hash_algorithm: str = "sha256"


class BridgeConfig(BaseModel):
    """Transport to CAD host: mock (in-process) or HTTP loopback (see docs/BRIDGE_TRANSPORT.md)."""

    mode: Literal["mock", "http"] = "mock"
    http_base_url: str = "http://127.0.0.1:8765"
    timeout_sec: float = Field(default=30.0, ge=1.0, le=120.0)
    mock_model_fingerprint: str = "stub-fingerprint"


class IntentValidationConfig(BaseModel):
    enforce_json_schema: bool = True
    enforce_command_allowlist: bool = True


class OrchestratorConfig(BaseModel):
    """Runtime config for the phase orchestrator."""

    model_config = ConfigDict(extra="allow")

    project: ProjectConfig = Field(default_factory=ProjectConfig)
    audit: AuditConfig = Field(default_factory=AuditConfig)
    bridge: BridgeConfig = Field(default_factory=BridgeConfig)
    intent_validation: IntentValidationConfig = Field(default_factory=IntentValidationConfig)
    max_verification_attempts: int = Field(default=3, ge=1, le=10)


class CadIntentEnvelope(BaseModel):
    """Validated intent crossing the LLM boundary before CAD execution.

    Parameters are intentionally loose (dict): command-specific shapes belong
    in the intent catalog and optional per-command Pydantic models later.
    """

    model_config = ConfigDict(extra="forbid")

    schemaVersion: str = Field(default=INTENT_SCHEMA_VERSION)
    intentId: str = Field(min_length=1)
    command: str = Field(min_length=1, description="Registered intent name; see INTENT_COMMAND_CATALOG.md")
    parameters: dict[str, Any] = Field(default_factory=dict)
    targetDocumentRef: str | None = None
    modelFingerprintExpected: str | None = Field(
        default=None,
        description="Optional hash/token from CAD add-in; orchestrator refuses dispatch if mismatch after sync.",
    )


class PhaseResult(BaseModel):
    phase: str
    success: bool
    duration_sec: float = 0.0
    message: str = ""
    data: dict[str, Any] = Field(default_factory=dict)
    output_files: list[Path] = Field(default_factory=list)


class PipelineResult(BaseModel):
    project_id: str
    success: bool = True
    phases: list[PhaseResult] = Field(default_factory=list)
    output_files: list[Path] = Field(default_factory=list)
    errors: list[str] = Field(default_factory=list)


class OrchestratorContext(BaseModel):
    """Mutable-ish run context passed between phases (Pydantic v2 copy-friendly)."""

    model_config = ConfigDict(arbitrary_types_allowed=True)

    output_dir: Path
    intent: CadIntentEnvelope | None = None
    model_fingerprint_observed: str | None = None
    dispatch_ack: dict[str, Any] = Field(default_factory=dict)
    bridge_execution: dict[str, Any] | None = None
    verification: dict[str, Any] = Field(default_factory=dict)
    phase_status: dict[str, str] = Field(default_factory=dict)
    errors: list[str] = Field(default_factory=list)

    def merge_data(self, data: dict[str, Any]) -> None:
        for key, value in data.items():
            if hasattr(self, key):
                setattr(self, key, value)
