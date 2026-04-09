"""JSON Schema and command allowlist for CadIntentEnvelope (validate_intent phase)."""

from __future__ import annotations

import json
import os
from pathlib import Path

import jsonschema
from jsonschema import ValidationError

from ingenieer.models import CadIntentEnvelope, IntentValidationConfig

# MVP catalog — keep in sync with docs/INTENT_COMMAND_CATALOG.md
ALLOWED_COMMANDS: frozenset[str] = frozenset(
    {"NoOp", "PingHost", "GetModelFingerprint", "HighRiskStub"},
)

# Risk tier for human-confirmation rules (execute + high requires humanConfirmationToken).
COMMAND_RISK: dict[str, str] = {
    "NoOp": "low",
    "PingHost": "low",
    "GetModelFingerprint": "low",
    "HighRiskStub": "high",
}


def command_risk(command: str) -> str:
    return COMMAND_RISK.get(command, "low")


def default_intent_schema_path() -> Path:
    """Resolve repo `schemas/cad_intent_envelope.schema.json` from package location."""
    override = os.environ.get("INGENEER_SCHEMA_DIR")
    if override:
        return Path(override) / "cad_intent_envelope.schema.json"
    # orchestrator/src/ingenieer/ -> repo root
    here = Path(__file__).resolve()
    repo_root = here.parents[3]
    return repo_root / "schemas" / "cad_intent_envelope.schema.json"


def collect_intent_validation_errors(
    intent: CadIntentEnvelope,
    cfg: IntentValidationConfig,
) -> list[str]:
    errors: list[str] = []
    if cfg.enforce_command_allowlist and intent.command not in ALLOWED_COMMANDS:
        errors.append(f"command not in allowlist: {intent.command!r}")
    if cfg.enforce_json_schema:
        path = default_intent_schema_path()
        if not path.is_file():
            errors.append(f"intent JSON Schema not found at {path} (set INGENEER_SCHEMA_DIR if needed)")
        else:
            with path.open(encoding="utf-8") as f:
                schema = json.load(f)
            instance = intent.model_dump(mode="json")
            try:
                jsonschema.validate(instance, schema)
            except ValidationError as exc:
                errors.append(exc.message)

    if intent.executionMode == "execute" and command_risk(intent.command) == "high":
        token = (intent.humanConfirmationToken or "").strip()
        if not token:
            errors.append(
                "high-risk command in execute mode requires non-empty humanConfirmationToken "
                "(use dry_run or preview to plan without confirmation)"
            )

    return errors
