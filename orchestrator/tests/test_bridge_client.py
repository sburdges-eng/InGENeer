import json
import threading
import time
from datetime import timedelta
from email.utils import format_datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

import ingenieer.bridge_client as bridge_client_module
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


def test_http_client_sends_request_id_for_get():
    class HeaderHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerHeaderGet/1.0"
        request_ids: list[str] = []
        idempotency_keys: list[str | None] = []

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).request_ids.append(self.headers.get("X-Request-Id", ""))
            type(self).idempotency_keys.append(self.headers.get("X-Idempotency-Key"))
            body = b'{"modelFingerprint":"hdr-fp"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), HeaderHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=0, retry_backoff_sec=0.05)
    try:
        assert client.get_model_fingerprint() == "hdr-fp"
        assert HeaderHandler.request_ids == ["model-fingerprint:attempt-1"]
        assert HeaderHandler.idempotency_keys == [None]
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_sends_request_id_and_idempotency_key_for_post():
    class HeaderHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerHeaderPost/1.0"
        request_ids: list[str] = []
        idempotency_keys: list[str | None] = []

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_POST(self) -> None:
            type(self).request_ids.append(self.headers.get("X-Request-Id", ""))
            type(self).idempotency_keys.append(self.headers.get("X-Idempotency-Key"))
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
                        "modelFingerprintAfter": "hdr-fp",
                    },
                }
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), HeaderHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=0, retry_backoff_sec=0.05)
    intent = CadIntentEnvelope(intentId="exec-123", command="NoOp", parameters={})
    try:
        result = client.execute_intent(intent)
        assert result.success
        assert HeaderHandler.request_ids == ["exec-123:attempt-1"]
        assert HeaderHandler.idempotency_keys == ["exec-123"]
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_uses_retry_after_seconds_for_503():
    class RetryAfterHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerRetryAfterSeconds/1.0"
        seen = 0

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).seen += 1
            if type(self).seen == 1:
                body = b'{"error":"busy"}'
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Retry-After", "2")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            body = b'{"modelFingerprint":"retry-after-ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    class CapturingClient(HttpBridgeClient):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.sleep_calls: list[float] = []

        def _sleep_for(self, delay_sec: float) -> None:
            self.sleep_calls.append(delay_sec)

    server = ThreadingHTTPServer(("127.0.0.1", 0), RetryAfterHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = CapturingClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=1, retry_backoff_sec=0.05)
    try:
        assert client.get_model_fingerprint() == "retry-after-ok"
        assert client.sleep_calls == [2.0]
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_uses_retry_after_http_date_for_429(monkeypatch):
    class RetryAfterHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerRetryAfterDate/1.0"
        seen = 0

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).seen += 1
            if type(self).seen == 1:
                body = b'{"error":"throttled"}'
                self.send_response(429)
                self.send_header("Content-Type", "application/json")
                self.send_header("Retry-After", format_datetime(bridge_client_module._utc_now() + timedelta(seconds=3)))
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            body = b'{"modelFingerprint":"retry-date-ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    class CapturingClient(HttpBridgeClient):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.sleep_calls: list[float] = []

        def _sleep_for(self, delay_sec: float) -> None:
            self.sleep_calls.append(delay_sec)

    fixed_now = bridge_client_module._utc_now().replace(microsecond=0)
    monkeypatch.setattr(bridge_client_module, "_utc_now", lambda: fixed_now)

    server = ThreadingHTTPServer(("127.0.0.1", 0), RetryAfterHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = CapturingClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=1, retry_backoff_sec=0.05)
    try:
        assert client.get_model_fingerprint() == "retry-date-ok"
        assert client.sleep_calls == [3.0]
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_invalid_retry_after_falls_back_to_default_backoff():
    class RetryAfterHandler(BaseHTTPRequestHandler):
        server_version = "InGENeerRetryAfterFallback/1.0"
        seen = 0

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).seen += 1
            if type(self).seen == 1:
                body = b'{"error":"busy"}'
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Retry-After", "not-a-delay")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            body = b'{"modelFingerprint":"retry-fallback-ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    class CapturingClient(HttpBridgeClient):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.sleep_calls: list[float] = []

        def _sleep_for(self, delay_sec: float) -> None:
            self.sleep_calls.append(delay_sec)

        def _default_backoff_delay(self, attempt: int) -> float:
            return 0.75

    server = ThreadingHTTPServer(("127.0.0.1", 0), RetryAfterHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = CapturingClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=1, retry_backoff_sec=0.05)
    try:
        assert client.get_model_fingerprint() == "retry-fallback-ok"
        assert client.sleep_calls == [0.75]
    finally:
        client.close()
        server.shutdown()
        server.server_close()


def test_http_client_verify_uses_verify_fingerprint_request_id():
    class VerifyHeaderHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "InGENeerVerifyHeader/1.0"
        get_request_ids: list[str] = []
        post_request_ids: list[str] = []

        def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
            return

        def do_GET(self) -> None:
            type(self).get_request_ids.append(self.headers.get("X-Request-Id", ""))
            body = b'{"modelFingerprint":"verify-fp"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self) -> None:
            type(self).post_request_ids.append(self.headers.get("X-Request-Id", ""))
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
                        "modelFingerprintAfter": "verify-fp",
                    },
                }
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), VerifyHeaderHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    host, port = server.server_address
    client = HttpBridgeClient(f"http://{host}:{port}", timeout_sec=1.0, max_retries=0, retry_backoff_sec=0.05)
    intent = CadIntentEnvelope(intentId="verify-123", command="NoOp", parameters={})
    try:
        execute_result = client.execute_intent(intent)
        assert execute_result.success
        verify_result = client.verify_post_dispatch(intent, execute_result.telemetry)
        assert verify_result.success
        assert VerifyHeaderHandler.post_request_ids == ["verify-123:attempt-1"]
        assert VerifyHeaderHandler.get_request_ids == ["verify-fingerprint:verify-123:attempt-1"]
    finally:
        client.close()
        server.shutdown()
        server.server_close()
