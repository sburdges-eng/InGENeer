from ingenieer.audit import AuditLogger
from ingenieer.bridge_client import MockBridgeClient
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.orchestrator import PipelineOrchestrator
from ingenieer.wire import BridgeExecutionResult


class StaleFingerPrintBridge(MockBridgeClient):
    """Returns success but never changes the fingerprint (simulates silent commit failure)."""

    def execute_intent(self, intent):
        fp = self._fingerprint  # frozen — no mutation
        return BridgeExecutionResult(
            success=True,
            stdout=f"mock:{intent.command}",
            telemetry={
                "intentId": intent.intentId,
                "command": intent.command,
                "executionMode": intent.executionMode,
                "modelFingerprintAfter": fp,
            },
        )


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


def test_pipeline_preview_mode(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="preview-1",
        command="NoOp",
        parameters={},
        executionMode="preview",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


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


def test_verify_surface_returns_query_telemetry(tmp_path):
    """VerifySurface must return point_count, triangle_count, bounds in telemetry."""
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="vs-telemetry-1",
        command="VerifySurface",
        parameters={"surface_name": "Existing Ground"},
        executionMode="execute",
    )
    result = orch.run(intent)
    assert result.success

    # Extract telemetry from the dispatch_execute phase
    dispatch_phase = next(p for p in result.phases if p.phase == "dispatch_execute")
    be = dispatch_phase.data.get("bridge_execution", {})
    telemetry = be.get("telemetry", {})

    assert telemetry["command"] == "VerifySurface"
    assert telemetry["point_count"] == 1024
    assert telemetry["triangle_count"] == 2000
    assert isinstance(telemetry["bounds"], list)
    assert len(telemetry["bounds"]) == 2
    assert telemetry["bounds"][0] == [0.0, 0.0, 0.0]
    assert telemetry["bounds"][1] == [1000.0, 1000.0, 50.0]


def test_pipeline_create_cross_section_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="xsec-pipe-1",
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
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_cross_section_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="xsec-pipe-2",
        command="CreateCrossSection",
        parameters={
            "alignment_name": "Bypass Rd CL",
            "profile_name": "Design Grade",
            "template_name": "Rural 2-Lane",
            "stations": [100.0, 200.0, 300.0],
            "layer": "C-ROAD-XSEC",
        },
        executionMode="execute",
        humanConfirmationToken="approved-xsec-123",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_corridor_model_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="corr-pipe-1",
        command="CreateCorridorModel",
        parameters={
            "name": "Phase 1 Road",
            "alignment_name": "Main St CL",
            "profile_name": "Finished Grade",
            "layer": "C-ROAD-CORR",
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


def test_pipeline_create_corridor_model_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="corr-pipe-2",
        command="CreateCorridorModel",
        parameters={
            "name": "Bypass Rd Corridor",
            "alignment_name": "Bypass Rd CL",
            "profile_name": "Design Grade",
            "layer": "C-ROAD-CORR",
        },
        executionMode="execute",
        humanConfirmationToken="approved-corr-456",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_balance_grading_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="bg-pipe-1",
        command="BalanceGrading",
        parameters={
            "existing_surface": "Existing Ground",
            "proposed_surface": "Proposed Grade",
            "tolerance": 10.0,
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


def test_pipeline_balance_grading_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="bg-pipe-2",
        command="BalanceGrading",
        parameters={
            "existing_surface": "Existing Ground",
            "proposed_surface": "Proposed Grade",
            "tolerance": 5.0,
            "shrink_swell_factor": 1.15,
        },
        executionMode="execute",
        humanConfirmationToken="approved-bg-123",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_retention_pond_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="pond-pipe-1",
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
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_retention_pond_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="pond-pipe-2",
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
        humanConfirmationToken="approved-pond-456",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_sanitary_sewer_network_dry_run(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="sewer-pipe-1",
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
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_create_sanitary_sewer_network_execute_with_token(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="sewer-pipe-2",
        command="CreateSanitarySewerNetwork",
        parameters={
            "network_name": "Phase 2 Sewer",
            "alignment_name": "Bypass Rd CL",
            "structures": [
                {"station": 100.0, "type": "Manhole 48in", "rim_elevation": 95.0, "invert_elevation": 87.0},
                {"station": 250.0, "type": "Manhole 48in", "rim_elevation": 92.0, "invert_elevation": 85.0},
            ],
            "pipe_material": "HDPE",
            "pipe_diameter": 10.0,
            "layer": "C-SSWR-NETW",
        },
        executionMode="execute",
        humanConfirmationToken="approved-sewer-789",
    )
    result = orch.run(intent)
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]


def test_pipeline_analyze_storm_drainage_execute_no_token(tmp_path):
    """AnalyzeStormDrainage is low-risk: execute mode works without confirmation token."""
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out)
    intent = CadIntentEnvelope(
        intentId="storm-pipe-1",
        command="AnalyzeStormDrainage",
        parameters={
            "network_name": "Phase 1 Storm",
            "design_storm_years": 25,
            "runoff_coefficient": 0.85,
        },
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


def test_silent_mutation_failure_detected(tmp_path):
    """High-risk execute that doesn't change fingerprint must fail verification."""
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    bridge = StaleFingerPrintBridge()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out, bridge=bridge)
    intent = CadIntentEnvelope(
        intentId="silent-fail-1",
        command="DrawPolylineFromCoordinates",
        parameters={
            "points": [[0, 0, 0], [10, 10, 1]],
            "layer": "BOUNDARY",
            "closed": False,
        },
        executionMode="execute",
        humanConfirmationToken="operator-approved",
    )
    result = orch.run(intent)
    assert not result.success
    assert result.phases[-1].phase == "verify_result"
    assert "silent mutation failure" in result.phases[-1].message.lower()


def test_low_risk_execute_bypasses_silent_mutation_check(tmp_path):
    """Low-risk execute with unchanged fingerprint must NOT trigger silent mutation failure."""
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="t")
    out = tmp_path / "out"
    out.mkdir()
    bridge = StaleFingerPrintBridge()
    orch = PipelineOrchestrator(OrchestratorConfig(), audit, out, bridge=bridge)
    intent = CadIntentEnvelope(
        intentId="low-risk-no-fail-1",
        command="AnalyzeStormDrainage",
        parameters={
            "network_name": "Phase 1 Storm",
            "design_storm_years": 25,
            "runoff_coefficient": 0.85,
        },
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
