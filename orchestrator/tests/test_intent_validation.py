from pathlib import Path

import pytest

from ingenieer.intent_validation import (
    ALLOWED_COMMANDS,
    collect_intent_validation_errors,
    default_intent_schema_path,
)
from ingenieer.models import CadIntentEnvelope, IntentValidationConfig


def test_default_schema_path_points_at_repo_file():
    p = default_intent_schema_path()
    assert p.name == "cad_intent_envelope.schema.json"
    assert p.parent.name == "schemas"


def test_allowlist_rejects_unknown_command():
    intent = CadIntentEnvelope(intentId="i", command="UnknownCmd", parameters={})
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("allowlist" in e for e in errs)


def test_allowlist_accepts_catalog_commands():
    for cmd in ALLOWED_COMMANDS:
        intent = CadIntentEnvelope(intentId="i", command=cmd, parameters={})
        errs = collect_intent_validation_errors(
            intent,
            IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
        )
        assert errs == []


@pytest.mark.skipif(
    not Path(__file__).resolve().parents[2].joinpath("schemas", "cad_intent_envelope.schema.json").is_file(),
    reason="schema file not at expected repo layout",
)
def test_json_schema_rejects_bad_schema_version():
    intent = CadIntentEnvelope(
        intentId="i",
        command="NoOp",
        parameters={},
        schemaVersion="9.9.9",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=False),
    )
    assert errs
