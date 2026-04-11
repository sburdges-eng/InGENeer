import pytest

from ingenieer.audit import AuditLogger
from ingenieer.batch import BatchPipeline, BatchResult, ProjectContract
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig


def test_project_contract_parses_minimal():
    raw = {
        "project": {"name": "site-grading", "version": "1.0.0"},
        "intents": [
            {"intentId": "s1", "command": "NoOp", "parameters": {}},
        ],
    }
    pc = ProjectContract.model_validate(raw)
    assert pc.project.name == "site-grading"
    assert len(pc.intents) == 1
    assert isinstance(pc.intents[0], CadIntentEnvelope)


def test_project_contract_rejects_empty_intents():
    with pytest.raises(Exception):
        ProjectContract.model_validate(
            {"project": {"name": "empty"}, "intents": []}
        )


def test_batch_result_defaults():
    br = BatchResult(project_id="test")
    assert br.success is True
    assert br.completed_steps == 0
    assert br.total_steps == 0
    assert br.step_results == []
    assert br.last_good_fingerprint is None



def test_batch_runs_all_intents(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-test")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-test"},
        "intents": [
            {"intentId": "b1", "command": "NoOp", "parameters": {}},
            {"intentId": "b2", "command": "PingHost", "parameters": {}},
            {"intentId": "b3", "command": "GetModelFingerprint", "parameters": {}},
        ],
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)
    assert result.success
    assert result.completed_steps == 3
    assert result.total_steps == 3
    assert len(result.step_results) == 3
    assert all(sr.pipeline_result.success for sr in result.step_results)


def test_batch_halts_on_failure(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-fail")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-fail"},
        "intents": [
            {"intentId": "ok1", "command": "NoOp", "parameters": {}},
            {
                "intentId": "fail1",
                "command": "DrawPolylineFromCoordinates",
                "parameters": {"points": [[0, 0, 0], [1, 1, 1]], "layer": "X", "closed": False},
                "executionMode": "execute",
            },
            {"intentId": "never", "command": "NoOp", "parameters": {}},
        ],
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)
    assert not result.success
    assert result.completed_steps == 1
    assert result.total_steps == 3
    assert len(result.step_results) == 2
    assert result.step_results[0].pipeline_result.success
    assert not result.step_results[1].pipeline_result.success


def test_batch_threads_fingerprint(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-fp")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-fp"},
        "intents": [
            {"intentId": "fp1", "command": "NoOp", "parameters": {}},
            {"intentId": "fp2", "command": "NoOp", "parameters": {}},
        ],
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)
    assert result.success
    assert result.step_results[1].fingerprint_before is not None
    assert result.step_results[0].fingerprint_after == result.step_results[1].fingerprint_before


def test_batch_execution_mode_override(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-dry")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-dry"},
        "intents": [
            {
                "intentId": "dry1",
                "command": "DrawPolylineFromCoordinates",
                "parameters": {"points": [[0, 0, 0], [1, 1, 1]], "layer": "X", "closed": False},
                "executionMode": "execute",
            },
        ],
        "execution_mode_override": "dry_run",
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)
    assert result.success


def test_batch_duplicate_intent_ids_rejected():
    with pytest.raises(Exception, match="duplicate"):
        ProjectContract.model_validate({
            "project": {"name": "dupes"},
            "intents": [
                {"intentId": "same", "command": "NoOp", "parameters": {}},
                {"intentId": "same", "command": "PingHost", "parameters": {}},
            ],
        })
