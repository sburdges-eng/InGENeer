from ingenieer.bridge_client import MockBridgeClient
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.wire import BridgeExecutionResult


def test_mock_fingerprint():
    c = MockBridgeClient("fp-1")
    assert c.get_model_fingerprint() == "fp-1"


def test_mock_execute_noop():
    c = MockBridgeClient()
    intent = CadIntentEnvelope(intentId="a", command="NoOp", parameters={})
    r = c.execute_intent(intent)
    assert isinstance(r, BridgeExecutionResult)
    assert r.success
    assert r.telemetry["command"] == "NoOp"


def test_mock_execute_fail_flag():
    c = MockBridgeClient()
    intent = CadIntentEnvelope(
        intentId="a",
        command="NoOp",
        parameters={"_bridge_execute_fail": True},
    )
    r = c.execute_intent(intent)
    assert not r.success
    assert r.error_traceback


def test_create_bridge_from_config():
    from ingenieer.bridge_client import HttpBridgeClient, MockBridgeClient, create_bridge_client

    m = create_bridge_client(OrchestratorConfig(bridge={"mode": "mock", "mock_model_fingerprint": "x"}))
    assert isinstance(m, MockBridgeClient)
    assert m.get_model_fingerprint() == "x"

    h = create_bridge_client(OrchestratorConfig(bridge={"mode": "http", "http_base_url": "http://127.0.0.1:9"}))
    assert isinstance(h, HttpBridgeClient)
