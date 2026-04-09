from ingenieer.contracts import validate_contract_payload
from ingenieer.wire import BridgeExecutionResult


def test_bridge_result_contract():
    br = BridgeExecutionResult(success=True, stdout="ok")
    payload = br.as_contract()
    assert validate_contract_payload(payload) == []
    assert payload["artifactType"] == "icad_bridge_execution_result"
