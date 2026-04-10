from pathlib import Path

import pytest

from ingenieer.intent_validation import (
    ALLOWED_COMMANDS,
    COMMAND_RISK,
    collect_intent_validation_errors,
    command_risk,
    default_intent_schema_path,
)
from ingenieer.models import CadIntentEnvelope, IntentValidationConfig


def test_default_schema_path_points_at_repo_file():
    p = default_intent_schema_path()
    assert p.name == "cad_intent_envelope.schema.json"
    assert p.parent.name == "schemas"


def test_create_alignment_schema_validation_success():
    """Valid CreateAlignment intent should pass with enforce_json_schema=True."""
    intent = CadIntentEnvelope(
        intentId="a1",
        command="CreateAlignment",
        parameters={
            "name": "Main St CL",
            "points": [[1000.0, 2000.0, 100.0], [1200.0, 2100.0, 101.5]],
            "start_station": 0.0,
            "layer": "ALIGNMENT"
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_alignment_schema_validation_failure_missing_field():
    """Missing required field in CreateAlignment should fail with deep validation."""
    intent = CadIntentEnvelope(
        intentId="a2",
        command="CreateAlignment",
        parameters={
            "name": "Missing Points",
            # points is missing
            "start_station": 0.0,
            "layer": "ALIGNMENT"
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=True),
    )
    assert any("'points' is a required property" in e for e in errs)


def test_create_alignment_schema_validation_failure_too_few_points():
    """CreateAlignment with only one point should fail the minItems: 2 constraint."""
    intent = CadIntentEnvelope(
        intentId="a3",
        command="CreateAlignment",
        parameters={
            "name": "Single Point",
            "points": [[1000.0, 2000.0, 100.0]], # Only 1 point
            "start_station": 0.0,
            "layer": "ALIGNMENT"
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=True),
    )
    assert any("is too short" in e for e in errs)


def test_create_alignment_schema_validation_failure_wrong_coordinate_type():
    """Coordinate list with strings instead of numbers should fail."""
    intent = CadIntentEnvelope(
        intentId="a4",
        command="CreateAlignment",
        parameters={
            "name": "Wrong Type",
            "points": [["x", "y", "z"], ["1", "2", "3"]], # Strings
            "start_station": 0.0,
            "layer": "ALIGNMENT"
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=True),
    )
    assert any("is not of type 'number'" in e for e in errs)


def test_create_alignment_schema_validation_failure_bad_enum():
    """Creating an alignment with an unknown type should fail the enum constraint."""
    intent = CadIntentEnvelope(
        intentId="a5",
        command="CreateAlignment",
        parameters={
            "name": "Bad Enum",
            "points": [[0.0, 0.0, 0.0], [1.0, 1.0, 0.0]],
            "start_station": 0.0,
            "layer": "ALIGNMENT",
            "type": "imaginary-type" # Not in enum
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=True),
    )
    assert any("is not one of" in e for e in errs)


def test_ping_host_schema_validation_success():
    """Simple intents with empty schemas should pass validation."""
    intent = CadIntentEnvelope(intentId="p1", command="PingHost", parameters={})
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=True, enforce_command_allowlist=True),
    )
    assert errs == []


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


def test_create_alignment_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="align-1",
        command="CreateAlignment",
        parameters={
            "name": "Main St CL",
            "points": [[1000, 2000, 100], [1200, 2100, 101.5], [1500, 2050, 99.8]],
            "start_station": 0.0,
            "layer": "ALIGNMENT",
            "type": "centerline",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_alignment_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="align-2",
        command="CreateAlignment",
        parameters={
            "name": "Main St CL",
            "points": [[1000, 2000, 100], [1200, 2100, 101.5]],
            "start_station": 0.0,
            "layer": "ALIGNMENT",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_alignment_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="align-3",
        command="CreateAlignment",
        parameters={
            "name": "Bypass Rd CL",
            "points": [[0, 0, 0], [500, 300, 10], [1000, 200, 5]],
            "start_station": 100.0,
            "layer": "ALIGNMENT",
            "type": "offset",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-align",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_profile_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="prof-1",
        command="CreateProfile",
        parameters={
            "alignment_name": "Main St CL",
            "profile_name": "Finished Grade",
            "pvi_data": [
                {"station": 0.0, "elevation": 100.0},
                {"station": 250.0, "elevation": 105.0},
                {"station": 538.52, "elevation": 102.0},
            ],
            "layer": "C-ROAD-PROF",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_profile_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="prof-2",
        command="CreateProfile",
        parameters={
            "alignment_name": "Main St CL",
            "profile_name": "Existing Ground",
            "pvi_data": [
                {"station": 0.0, "elevation": 100.0},
                {"station": 538.52, "elevation": 102.0},
            ],
            "layer": "C-ROAD-PROF",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_profile_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="prof-3",
        command="CreateProfile",
        parameters={
            "alignment_name": "Bypass Rd CL",
            "profile_name": "Design Grade",
            "pvi_data": [
                {"station": 100.0, "elevation": 50.0},
                {"station": 300.0, "elevation": 55.0},
                {"station": 600.0, "elevation": 48.0},
            ],
            "layer": "C-ROAD-PROF",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-prof",
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


def test_create_cross_section_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="xsec-1",
        command="CreateCrossSection",
        parameters={
            "alignment_name": "Main St CL",
            "profile_name": "Finished Grade",
            "template_name": "Standard Road",
            "stations": [0.0, 50.0, 100.0, 250.0, 500.0],
            "layer": "C-ROAD-XSEC",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_cross_section_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="xsec-2",
        command="CreateCrossSection",
        parameters={
            "alignment_name": "Main St CL",
            "profile_name": "Finished Grade",
            "template_name": "Standard Road",
            "stations": [0.0, 100.0],
            "layer": "C-ROAD-XSEC",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_cross_section_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="xsec-3",
        command="CreateCrossSection",
        parameters={
            "alignment_name": "Bypass Rd CL",
            "profile_name": "Design Grade",
            "template_name": "Rural 2-Lane",
            "stations": [100.0, 200.0, 300.0],
            "layer": "C-ROAD-XSEC",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-xsec",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_corridor_model_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="corr-1",
        command="CreateCorridorModel",
        parameters={
            "name": "Phase 1 Road",
            "alignment_name": "Main St CL",
            "profile_name": "Finished Grade",
            "layer": "C-ROAD-CORR",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_corridor_model_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="corr-2",
        command="CreateCorridorModel",
        parameters={
            "name": "Phase 1 Road",
            "alignment_name": "Main St CL",
            "profile_name": "Finished Grade",
            "layer": "C-ROAD-CORR",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_corridor_model_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="corr-3",
        command="CreateCorridorModel",
        parameters={
            "name": "Bypass Rd Corridor",
            "alignment_name": "Bypass Rd CL",
            "profile_name": "Design Grade",
            "layer": "C-ROAD-CORR",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-corr",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_balance_grading_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="bg-1",
        command="BalanceGrading",
        parameters={
            "existing_surface": "Existing Ground",
            "proposed_surface": "Proposed Grade",
            "tolerance": 10.0,
            "shrink_swell_factor": 1.15,
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_balance_grading_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="bg-2",
        command="BalanceGrading",
        parameters={
            "existing_surface": "Existing Ground",
            "proposed_surface": "Proposed Grade",
            "tolerance": 10.0,
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_balance_grading_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="bg-3",
        command="BalanceGrading",
        parameters={
            "existing_surface": "Existing Ground",
            "proposed_surface": "Proposed Grade",
            "tolerance": 5.0,
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-bg",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_retention_pond_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="pond-1",
        command="CreateRetentionPond",
        parameters={
            "outline_polyline_id": "pond-limit-01",
            "base_elevation": 95.0,
            "side_slope": 3.0,
            "target_surface": "Existing Ground",
            "layer": "C-TOPO-POND",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_retention_pond_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="pond-2",
        command="CreateRetentionPond",
        parameters={
            "outline_polyline_id": "pond-limit-01",
            "base_elevation": 95.0,
            "side_slope": 3.0,
            "target_surface": "Existing Ground",
            "layer": "C-TOPO-POND",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_retention_pond_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="pond-3",
        command="CreateRetentionPond",
        parameters={
            "outline_polyline_id": "pond-limit-02",
            "base_elevation": 90.0,
            "side_slope": 4.0,
            "berm_width": 10.0,
            "target_surface": "Existing Ground",
            "layer": "C-TOPO-POND",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-pond",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_sanitary_sewer_network_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="sewer-1",
        command="CreateSanitarySewerNetwork",
        parameters={
            "network_name": "Phase 1 Sewer",
            "alignment_name": "Main St CL",
            "structures": [
                {"station": 0.0, "type": "Manhole 48in", "rim_elevation": 100.0, "invert_elevation": 92.0},
                {"station": 250.0, "type": "Manhole 48in", "rim_elevation": 105.0, "invert_elevation": 91.5},
            ],
            "pipe_material": "PVC",
            "pipe_diameter": 8.0,
            "layer": "C-SSWR-NETW",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_sanitary_sewer_network_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="sewer-2",
        command="CreateSanitarySewerNetwork",
        parameters={
            "network_name": "Phase 1 Sewer",
            "alignment_name": "Main St CL",
            "structures": [
                {"station": 0.0, "type": "Manhole 48in", "rim_elevation": 100.0, "invert_elevation": 92.0},
            ],
            "pipe_material": "PVC",
            "pipe_diameter": 8.0,
            "layer": "C-SSWR-NETW",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_sanitary_sewer_network_execute_with_token_passes():
    intent = CadIntentEnvelope(
        intentId="sewer-3",
        command="CreateSanitarySewerNetwork",
        parameters={
            "network_name": "Phase 2 Sewer",
            "alignment_name": "Bypass Rd CL",
            "structures": [
                {"station": 100.0, "type": "Manhole 48in", "rim_elevation": 95.0, "invert_elevation": 87.0},
            ],
            "pipe_material": "HDPE",
            "pipe_diameter": 10.0,
            "layer": "C-SSWR-NETW",
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved-sewer",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_analyze_storm_drainage_accepted_no_token_needed():
    """AnalyzeStormDrainage is low-risk: execute mode works without confirmation token."""
    intent = CadIntentEnvelope(
        intentId="storm-1",
        command="AnalyzeStormDrainage",
        parameters={
            "network_name": "Phase 1 Storm",
            "design_storm_years": 25,
            "runoff_coefficient": 0.85,
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_place_planting_layout_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="plant-1",
        command="PlacePlantingLayout",
        parameters={
            "species_id": "QUERCUS_AGRIFOLIA",
            "points": [{"location": [1000, 2000, 0]}],
            "mature_spread": 40.0,
            "layer": "L-PLNT-TREE",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_place_planting_layout_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="plant-2",
        command="PlacePlantingLayout",
        parameters={
            "species_id": "QUERCUS_AGRIFOLIA",
            "points": [{"location": [1000, 2000, 0]}],
            "mature_spread": 40.0,
            "layer": "L-PLNT-TREE",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_create_paving_area_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="pave-1",
        command="CreatePavingArea",
        parameters={
            "boundary_points": [[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0]],
            "material_type": "Concrete",
            "subbase_depth": 1.0,
            "permeability_coefficient": 0.1,
            "layer": "L-HRDS-PAVE",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_create_paving_area_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="pave-2",
        command="CreatePavingArea",
        parameters={
            "boundary_points": [[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0]],
            "material_type": "Concrete",
            "subbase_depth": 1.0,
            "permeability_coefficient": 0.1,
            "layer": "L-HRDS-PAVE",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_design_irrigation_zone_accepted_dry_run():
    intent = CadIntentEnvelope(
        intentId="irrig-1",
        command="DesignIrrigationZone",
        parameters={
            "zone_id": "HYDROZONE-1",
            "heads": [{"location": [0, 0, 0], "type": "Rotor", "radius": 30.0}],
            "pipe_material": "PVC",
            "target_psi": 50.0,
            "layer": "L-IRRG-ZONE",
        },
        executionMode="dry_run",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert errs == []


def test_design_irrigation_zone_execute_requires_token():
    intent = CadIntentEnvelope(
        intentId="irrig-2",
        command="DesignIrrigationZone",
        parameters={
            "zone_id": "HYDROZONE-1",
            "heads": [{"location": [0, 0, 0], "type": "Rotor", "radius": 30.0}],
            "pipe_material": "PVC",
            "target_psi": 50.0,
            "layer": "L-IRRG-ZONE",
        },
        executionMode="execute",
    )
    errs = collect_intent_validation_errors(
        intent,
        IntentValidationConfig(enforce_json_schema=False, enforce_command_allowlist=True),
    )
    assert any("humanConfirmationToken" in e for e in errs)


def test_allowed_commands_and_risk_in_sync():
    """ALLOWED_COMMANDS and COMMAND_RISK must cover exactly the same set of commands."""
    assert ALLOWED_COMMANDS == set(COMMAND_RISK.keys())


def test_command_risk_values_are_valid_tiers():
    """All COMMAND_RISK values must be 'low' or 'high' — no typos like 'lo' or 'medium'."""
    for cmd, tier in COMMAND_RISK.items():
        assert tier in ("low", "high"), f"{cmd} has invalid risk tier: {tier!r}"


def test_command_risk_defaults_to_high_for_unknown():
    """Unknown commands must default to 'high' (fail-closed), not 'low'."""
    assert command_risk("NotARealCommand") == "high"


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
