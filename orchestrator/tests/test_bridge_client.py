import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from ingenieer.bridge_client import HttpBridgeClient, MockBridgeClient
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
    from ingenieer.bridge_client import create_bridge_client

    m = create_bridge_client(OrchestratorConfig(bridge={"mode": "mock", "mock_model_fingerprint": "x"}))
    assert isinstance(m, MockBridgeClient)
    assert m.get_model_fingerprint() == "x"

    h = create_bridge_client(OrchestratorConfig(bridge={"mode": "http", "http_base_url": "http://127.0.0.1:9"}))
    assert isinstance(h, HttpBridgeClient)


def test_http_client_retries_transient_503_then_succeeds():
    class RetryHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerRetryTest/1.0"
        request_count = 0

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).request_count += 1
            if type(self).request_count == 1:
                body = b'{"error":"temporary overload"}'
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            body = b'{"modelFingerprint":"retry-ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), RetryHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=1, retry_backoff_sec=0.05)
    try:
        assert client.get_model_fingerprint() == "retry-ok"
        assert RetryHandler.request_count == 2
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_verify_marks_timeout_as_transient():
    class SlowHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerTimeoutTest/1.0"

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            time.sleep(0.2)
            body = b'{"modelFingerprint":"too-late"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), SlowHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(f"http://{host}:{port}", timeout_sec=0.05, max_retries=0, retry_backoff_sec=0.05)
    intent = CadIntentEnvelope(intentId="timeout-1", command="NoOp", parameters={})
    try:
        result = client.verify_post_dispatch(
            intent,
            {"intentId": "timeout-1", "command": "NoOp", "modelFingerprintAfter": "fp"},
        )
        assert not result.success
        assert result.transient_failure is True
        assert "timeout" in result.message.lower()
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_reuses_keepalive_connection():
    class KeepAliveHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "InGENeerKeepAliveTest/1.0"
        client_ports: list[int] = []

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).client_ports.append(self.client_address[1])
            body = b'{"modelFingerprint":"keepalive-fp"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self) -> None:
            type(self).client_ports.append(self.client_address[1])
            length = int(self.headers.get("Content-Length", "0"))
            intent = json.loads(self.rfile.read(length).decode("utf-8"))
            body = json.dumps(
                {
                    "schemaVersion": "1.0.0",
                    "success": True,
                    "stdout": f"http:{intent['command']}",
                    "error_traceback": None,
                    "telemetry": {
                        "intentId": intent["intentId"],
                        "command": intent["command"],
                        "executionMode": intent.get("executionMode", "execute"),
                        "modelFingerprintAfter": "keepalive-fp",
                    },
                }
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), KeepAliveHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=0, retry_backoff_sec=0.05)
    intent = CadIntentEnvelope(intentId="keepalive-1", command="NoOp", parameters={})
    try:
        assert client.get_model_fingerprint() == "keepalive-fp"
        execute_result = client.execute_intent(intent)
        assert execute_result.success
        assert client.get_model_fingerprint() == "keepalive-fp"
        assert len(set(KeepAliveHandler.client_ports)) == 1
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_deadline_exceeded_before_retry_exhaustion():
    class DeadlineHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerDeadlineTest/1.0"

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            body = b'{"error":"temporary overload"}'
            self.send_response(503)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), DeadlineHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(
        f"http://{host}:{port}",
        timeout_sec=1.0,
        deadline_sec=0.08,
        max_retries=10,
        retry_backoff_sec=0.05,
    )
    try:
        with pytest.raises(RuntimeError, match="deadline exceeded"):
            client.get_model_fingerprint()
    finally:
        client.close()
        server.shutdown()
        server.server_close()
