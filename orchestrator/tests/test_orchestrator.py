from ingenieer.audit import AuditLogger
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.orchestrator import PipelineOrchestrator


def test_pipeline_all_phases_mock(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(intentId="i1", command="NoOp", parameters={})
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]
    v = result.phases[-1].data["verification"]
    assert v["status"] == "ok"
    assert v["source"] == "host_fingerprint"
    assert audit.verify_chain()[0]


def test_fingerprint_mismatch_fails_sync(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="i1",
        command="NoOp",
        parameters={},
        modelFingerprintExpected="real-hash",
    )
    result = orch.run(intent)
    assert not result.success
    assert result.phases[-1].phase == "sync_baseline"
    assert "fingerprint" in result.phases[-1].message.lower()


def test_dispatch_failure_skips_verify(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="i1",
        command="NoOp",
        parameters={"_bridge_execute_fail": True},
    )
    result = orch.run(intent)
    assert not result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
    ]


def test_verify_retries_transient_then_succeeds(tmp_path):
    from ingenieer.bridge_client import MockBridgeClient

    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    bridge = MockBridgeClient(verify_transient_failures=2)
    cfg = OrchestratorConfig(max_verification_attempts=4, verification_backoff_sec=0.05)
    orch = PipelineOrchestrator(cfg, audit, out, bridge=bridge)
    intent = CadIntentEnvelope(intentId="i1", command="NoOp", parameters={})
    result = orch.run(intent)
    assert result.success
    assert result.phases[-1].data["verification"]["attempts"] == 3


def test_high_risk_execute_fails_without_confirmation(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="i1",
        command="HighRiskStub",
        parameters={},
        executionMode="execute",
    )
    result = orch.run(intent)
    assert not result.success
    assert result.phases[-1].phase == "validate_intent"


def test_pipeline_draw_polyline_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="poly-pipe-1",
        command="DrawPolylineFromCoordinates",
        parameters={
            "points": [[0, 0, 0], [10, 10, 1], [20, 5, 2]],
            "layer": "BOUNDARY",
            "closed": True,
        },
        executionMode="dry_run",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_point_blocks_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="pts-pipe-1",
        command="CreatePointBlocks",
        parameters={
            "layer": "SURVEY",
            "blockName": "IRON_PIN",
            "points": [
                {"location": [1000, 2000, 345.67], "number": 101, "description": "IRON PIN"},
                {"location": [1050, 2010, 346.12], "number": 102, "description": "MAG NAIL"},
            ],
        },
        executionMode="dry_run",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_draw_polyline_execute_no_token_fails(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="poly-pipe-2",
        command="DrawPolylineFromCoordinates",
        parameters={
            "points": [[0, 0, 0], [10, 10, 1]],
            "layer": "TOPO",
            "closed": False,
        },
        executionMode="execute",
    )
    result = orch.run(intent)
    assert not result.success
    assert result.phases[-1].phase == "validate_intent"


def test_pipeline_create_point_blocks_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="pts-pipe-2",
        command="CreatePointBlocks",
        parameters={
            "layer": "CONTROL",
            "blockName": "BENCHMARK",
            "points": [
                {"location": [500, 600, 200.0], "number": 1, "elevation": 200.0},
            ],
        },
        executionMode="execute",
        humanConfirmationToken="approved-token-123",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_import_landxml_surface_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="lxml-pipe-1",
        command="ImportLandXmlSurface",
        parameters={
            "landxml_path_key": "surface_file",
            "surface_name": "Existing Ground",
            "layer": "TOPO_SURFACE",
        },
        executionMode="dry_run",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_import_landxml_surface_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="lxml-pipe-2",
        command="ImportLandXmlSurface",
        parameters={
            "landxml_path_key": "surface_file",
            "surface_name": "Proposed Grade",
            "layer": "DESIGN_SURFACE",
        },
        executionMode="execute",
        humanConfirmationToken="approved-lxml-456",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_alignment_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="align-pipe-1",
        command="CreateAlignment",
        parameters={
            "name": "Main St CL",
            "points": [[1000, 2000, 100], [1200, 2100, 101.5], [1500, 2050, 99.8]],
            "start_station": 0.0,
            "layer": "ALIGNMENT",
        },
        executionMode="dry_run",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_alignment_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="align-pipe-2",
        command="CreateAlignment",
        parameters={
            "name": "Bypass Rd CL",
            "points": [[0, 0, 0], [500, 300, 10], [1000, 200, 5]],
            "start_station": 100.0,
            "layer": "ALIGNMENT",
            "type": "centerline",
        },
        executionMode="execute",
        humanConfirmationToken="approved-align-789",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_profile_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="prof-pipe-1",
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
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_profile_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="prof-pipe-2",
        command="CreateProfile",
        parameters={
            "alignment_name": "Bypass Rd CL",
            "profile_name": "Design Grade",
            "pvi_data": [
                {"station": 100.0, "elevation": 50.0},
                {"station": 600.0, "elevation": 48.0},
            ],
            "layer": "C-ROAD-PROF",
        },
        executionMode="execute",
        humanConfirmationToken="approved-prof-101",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_verify_surface_execute_no_token(tmp_path):
    """VerifySurface is low-risk: execute mode works without confirmation token."""
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="vs-pipe-1",
        command="VerifySurface",
        parameters={"surface_name": "Existing Ground"},
        executionMode="execute",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_validate_rejects_unknown_command(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(intentId="i1", command="NotInCatalog", parameters={})
    result = orch.run(intent)
    assert not result.success
    assert result.phases[-1].phase == "validate_intent"
    assert "allowlist" in result.phases[-1].message.lower()
