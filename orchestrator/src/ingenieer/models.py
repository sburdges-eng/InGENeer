"""Pydantic models for CAD intent envelopes and pipeline results."""

from __future__ import annotations

import os
import tomllib
from pathlib import Path
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

INTENT_SCHEMA_VERSION = "1.1.0"

ExecutionMode = Literal["dry_run", "preview", "execute"]


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
    # Retries only for transient transport errors (connection reset, 503, etc.) — not for 4xx or bridge logic failures.
    http_max_retries: int = Field(default=2, ge=0, le=8)
    http_retry_backoff_sec: float = Field(default=0.25, ge=0.05, le=10.0)
    # Total wall-clock budget for one bridge call, across retries and backoff.
    deadline_sec: float = Field(default=60.0, ge=1.0, le=600.0)
    # Per-client circuit breaker trips after consecutive transient transport failures.
    circuit_breaker_threshold: int = Field(default=5, ge=1, le=50)
    circuit_breaker_cooldown_sec: float = Field(default=30.0, ge=1.0, le=300.0)


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
    verification_backoff_sec: float = Field(default=0.3, ge=0.05, le=30.0)

    @classmethod
    def from_env(cls) -> OrchestratorConfig:
        schema_enforce = _env_bool("INGENEER_SCHEMA_ENFORCE", True)
        return cls.model_validate(
            {
                "bridge": {
                    "mode": os.environ.get("INGENEER_BRIDGE_MODE", "mock"),
                    "http_base_url": os.environ.get(
                        "INGENEER_BRIDGE_URL",
                        "http://127.0.0.1:8765",
                    ),
                    "timeout_sec": _env_float("INGENEER_BRIDGE_TIMEOUT", 10.0),
                    "mock_model_fingerprint": os.environ.get(
                        "INGENEER_BRIDGE_FINGERPRINT",
                        "stub-fingerprint",
                    ),
                },
                "intent_validation": {
                    "enforce_json_schema": schema_enforce,
                    "enforce_command_allowlist": schema_enforce,
                },
                "max_verification_attempts": _env_int("INGENEER_MAX_VERIFY_ATTEMPTS", 3),
                "verification_backoff_sec": _env_float("INGENEER_VERIFY_BACKOFF", 0.5),
            }
        )

    @classmethod
    def from_toml(cls, path: str | Path) -> OrchestratorConfig:
        toml_path = Path(path)
        with toml_path.open("rb") as handle:
            raw = tomllib.load(handle)

        config_data = dict(raw)
        intent_validation = config_data.get("intent_validation")
        if isinstance(intent_validation, dict):
            intent_validation = dict(intent_validation)
            # Support the documented sample layout, where retry settings appear
            # after [intent_validation] and therefore parse inside that table.
            for key in ("max_verification_attempts", "verification_backoff_sec"):
                if key not in config_data and key in intent_validation:
                    config_data[key] = intent_validation.pop(key)
            config_data["intent_validation"] = intent_validation

        return cls.model_validate(config_data)


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
    executionMode: ExecutionMode = Field(
        default="execute",
        description="dry_run / preview: no committed mutation; execute: host may mutate (see catalog risk).",
    )
    humanConfirmationToken: str | None = Field(
        default=None,
        min_length=1,
        description="Required for high-risk commands when executionMode is execute (operator-issued).",
    )
    humanConfirmationId: str | None = Field(
        default=None,
        description="Optional correlation id linking confirmation to an external approval record.",
    )
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


def _env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    normalized = raw.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"{name} must be a boolean string, got {raw!r}")


def _env_float(name: str, default: float) -> float:
    raw = os.environ.get(name)
    return default if raw is None else float(raw)


def _env_int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    return default if raw is None else int(raw)
