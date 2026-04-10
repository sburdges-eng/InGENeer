"""JSON Schema and command allowlist for CadIntentEnvelope (validate_intent phase)."""

from __future__ import annotations

import json
import os
from pathlib import Path

import jsonschema
from jsonschema import ValidationError

from ingenieer.models import CadIntentEnvelope, IntentValidationConfig

# Intent command catalog — keep in sync with docs/INTENT_COMMAND_CATALOG.md
ALLOWED_COMMANDS: frozenset[str] = frozenset(
    {
        "NoOp",
        "PingHost",
        "GetModelFingerprint",
        "HighRiskStub",
        "DrawPolylineFromCoordinates",
        "CreatePointBlocks",
        "ImportLandXmlSurface",
        "VerifySurface",
        "CreateAlignment",
        "CreateProfile",
        "CreateCrossSection",
        "CreateCorridorModel",
        "BalanceGrading",
        "CreateRetentionPond",
        "CreateSanitarySewerNetwork",
        "AnalyzeStormDrainage",
        "PlacePlantingLayout",
        "CreatePavingArea",
        "DesignIrrigationZone",
    },
)

# Risk tier for human-confirmation rules (execute + high requires humanConfirmationToken).
COMMAND_RISK: dict[str, str] = {
    "NoOp": "low",
    "PingHost": "low",
    "GetModelFingerprint": "low",
    "HighRiskStub": "high",
    "DrawPolylineFromCoordinates": "high",
    "CreatePointBlocks": "high",
    "ImportLandXmlSurface": "high",
    "VerifySurface": "low",
    "CreateAlignment": "high",
    "CreateProfile": "high",
    "CreateCrossSection": "high",
    "CreateCorridorModel": "high",
    "BalanceGrading": "high",
    "CreateRetentionPond": "high",
    "CreateSanitarySewerNetwork": "high",
    "AnalyzeStormDrainage": "low",
    "PlacePlantingLayout": "high",
    "CreatePavingArea": "high",
    "DesignIrrigationZone": "high",
}

# Fail-fast: any desync between ALLOWED_COMMANDS and COMMAND_RISK crashes at import time.
assert set(COMMAND_RISK) == ALLOWED_COMMANDS, (
    f"COMMAND_RISK keys and ALLOWED_COMMANDS must match. "
    f"Missing from COMMAND_RISK: {ALLOWED_COMMANDS - set(COMMAND_RISK)}; "
    f"extra in COMMAND_RISK: {set(COMMAND_RISK) - ALLOWED_COMMANDS}"
)
assert all(v in ("low", "high") for v in COMMAND_RISK.values()), (
    "All COMMAND_RISK values must be 'low' or 'high'"
)


def command_risk(command: str) -> str:
    """Return the risk tier for *command*. Defaults to 'high' (fail-closed) for unknown commands."""
    return COMMAND_RISK.get(command, "high")


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

    schema_dir = default_intent_schema_path().parent

    if cfg.enforce_json_schema:
        # 1. Validate outer envelope
        envelope_path = schema_dir / "cad_intent_envelope.schema.json"
        if not envelope_path.is_file():
            errors.append(f"intent JSON Schema not found at {envelope_path} (set INGENEER_SCHEMA_DIR if needed)")
        else:
            with envelope_path.open(encoding="utf-8") as f:
                envelope_schema = json.load(f)
            instance = intent.model_dump(mode="json")
            try:
                jsonschema.validate(instance, envelope_schema)
            except ValidationError as exc:
                errors.append(f"Envelope validation: {exc.message}")

        # 2. Validate command-specific parameters
        if intent.command in ALLOWED_COMMANDS:
            params_path = schema_dir / "params" / f"{intent.command}.schema.json"
            if params_path.is_file():
                with params_path.open(encoding="utf-8") as f:
                    params_schema = json.load(f)
                try:
                    jsonschema.validate(intent.parameters, params_schema)
                except ValidationError as exc:
                    errors.append(f"Parameter validation ({intent.command}): {exc.message}")

    if intent.executionMode == "execute" and command_risk(intent.command) == "high":
        token = (intent.humanConfirmationToken or "").strip()
        if not token:
            errors.append(
                "high-risk command in execute mode requires non-empty humanConfirmationToken "
                "(use dry_run or preview to plan without confirmation)"
            )

    return errors
