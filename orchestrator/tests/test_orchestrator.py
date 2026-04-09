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
    assert result.phases[-1].data["verification"]["status"] == "ok"
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
