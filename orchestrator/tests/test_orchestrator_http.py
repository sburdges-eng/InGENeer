import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from ingenieer.audit import AuditLogger
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.orchestrator import PipelineOrchestrator


class _BridgeHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "InGENeerTest/1.0"
    fingerprint = "http-test-fp"

    def log_message(self, _format: str, *_args) -> None:  # noqa: ANN001
        return

    def do_GET(self) -> None:
        if self.path.split("?", 1)[0] != "/v1/model-fingerprint":
            self.send_error(404)
            return
        body = json.dumps({"modelFingerprint": self.fingerprint}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:
        if self.path.split("?", 1)[0] != "/v1/execute":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        intent = json.loads(raw.decode())
        cmd = intent.get("command", "")
        mode = intent.get("executionMode", "execute")
        fp = self.__class__.fingerprint
        if mode in ("dry_run", "preview"):
            after = fp
        else:
            self.__class__.fingerprint = f"{fp}:m"
            after = self.__class__.fingerprint
        result = {
            "schemaVersion": "1.0.0",
            "success": True,
            "stdout": f"http:{cmd}",
            "error_traceback": None,
            "telemetry": {
                "intentId": intent.get("intentId"),
                "command": cmd,
                "executionMode": mode,
                "modelFingerprintAfter": after,
            },
        }
        body = json.dumps(result).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def test_pipeline_http_mode_roundtrip(tmp_path):
    _BridgeHandler.fingerprint = "http-test-fp"
    server = ThreadingHTTPServer(("127.0.0.1", 0), _BridgeHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    host, port = server.server_address
    base = f"http://{host}:{port}"

    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="http_t")
    out = tmp_path / "out"
    out.mkdir()
    cfg = OrchestratorConfig(
        bridge={"mode": "http", "http_base_url": base, "timeout_sec": 5.0},
        intent_validation={"enforce_json_schema": True, "enforce_command_allowlist": True},
    )
    orch = PipelineOrchestrator(cfg, audit, out)
    intent = CadIntentEnvelope(
        intentId="h1",
        command="NoOp",
        parameters={},
        modelFingerprintExpected=_BridgeHandler.fingerprint,
    )
    result = orch.run(intent)
    server.shutdown()
    assert result.success
    assert [p.phase for p in result.phases] == [
        "validate_intent",
        "sync_baseline",
        "dispatch_execute",
        "verify_result",
    ]
    assert result.phases[-2].data["dispatch_ack"]["status"] == "executed"
