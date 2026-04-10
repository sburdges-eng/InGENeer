from ingenieer.batch import BatchResult, ProjectContract
from ingenieer.models import CadIntentEnvelope


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
    import pytest

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
