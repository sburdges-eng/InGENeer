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
    from ingenieer.intent_validation import command_risk
    for cmd in ALLOWED_COMMANDS:
        if command_risk(cmd) == "high":
            intent = CadIntentEnvelope(
                intentId="i",
                command=cmd,
                parameters={},
                executionMode="dry_run",
            )
        else:
            intent = CadIntentEnvelope(intentId="i", command=cmd, parameters={})
        errs = collect_intent_validation_errors(
            intent,
            IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
        )
        assert errs == []


def test_high_risk_execute_requires_human_token():
    intent = CadIntentEnvelope(
        intentId="i",
        command="HighRiskStub",
        parameters={},
        executionMode="execute",
        humanConfirmationToken=None,
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_high_risk_dry_run_skips_confirmation():
    intent = CadIntentEnvelope(
        intentId="i",
        command="HighRiskStub",
        parameters={},
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_draw_polyline_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="poly-1",
        command="DrawPolylineFromCoordinates",
        parameters={
            "points": [[0, 0, 0], [10, 10, 1]],
            "layer": "BOUNDARY",
            "closed": False,
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_draw_polyline_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="poly-2",
        command="DrawPolylineFromCoordinates",
        parameters={
            "points": [[0, 0, 0], [10, 10, 1]],
            "layer": "BOUNDARY",
            "closed": True,
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_point_blocks_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="pts-1",
        command="CreatePointBlocks",
        parameters={
            "layer": "SURVEY",
            "blockName": "IRON_PIN",
            "points": [
                {"location": [1000, 2000, 345.67], "number": 101, "description": "IRON PIN"},
            ],
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_point_blocks_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="pts-2",
        command="CreatePointBlocks",
        parameters={
            "layer": "SURVEY",
            "blockName": "IRON_PIN",
            "points": [
                {"location": [1000, 2000, 345.67], "number": 101},
            ],
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_draw_polyline_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="poly-3",
        command="DrawPolylineFromCoordinates",
        parameters={
            "points": [[0, 0, 0], [10, 10, 1], [20, 5, 2]],
            "layer": "CENTERLINE",
            "closed": False,
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-abc",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_point_blocks_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="pts-3",
        command="CreatePointBlocks",
        parameters={
            "layer": "TOPO",
            "blockName": "GROUND_SHOT",
            "points": [
                {"location": [500, 600, 200.0], "number": 1, "elevation": 200.0, "description": "GS"},
                {"location": [510, 610, 201.5], "number": 2, "elevation": 201.5},
            ],
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-xyz",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_import_landxml_surface_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="lxml-1",
        command="ImportLandXmlSurface",
        parameters={
            "landxml_path_key": "surface_file",
            "surface_name": "Existing Ground",
            "layer": "TOPO_SURFACE",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_import_landxml_surface_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="lxml-2",
        command="ImportLandXmlSurface",
        parameters={
            "landxml_path_key": "surface_file",
            "surface_name": "Existing Ground",
            "layer": "TOPO_SURFACE",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_import_landxml_surface_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="lxml-3",
        command="ImportLandXmlSurface",
        parameters={
            "landxml_path_key": "surface_file",
            "surface_name": "Proposed Grade",
            "layer": "DESIGN_SURFACE",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-lxml",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_verify_surface_accepted_no_token_needed():
    intent = CadIntentEnvelope(
        intentId="vs-1",
        command="VerifySurface",
        parameters={"surface_name": "Existing Ground"},
        executionMode="execute",
    )
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
