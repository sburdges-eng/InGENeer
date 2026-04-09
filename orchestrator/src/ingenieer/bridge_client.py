"""CAD bridge clients: in-process mock and HTTP (loopback) per docs/BRIDGE_TRANSPORT.md."""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Protocol

from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.wire import BridgeExecutionResult


class BridgeClient(Protocol):
    def get_model_fingerprint(self) -> str: ...
    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult: ...


class MockBridgeClient:
    """Deterministic bridge for tests and CI (no network)."""

    def __init__(self, model_fingerprint: str = "stub-fingerprint") -> None:
        self._model_fingerprint = model_fingerprint

    def get_model_fingerprint(self) -> str:
        return self._model_fingerprint

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult:
        if intent.parameters.get("_bridge_execute_fail") is True:
            return BridgeExecutionResult(
                success=False,
                stdout="",
                error_traceback="mock failure (_bridge_execute_fail)",
                telemetry={"intentId": intent.intentId, "command": intent.command},
            )
        telemetry: dict[str, object] = {"intentId": intent.intentId, "command": intent.command}
        if intent.command == "PingHost":
            telemetry["hostId"] = "mock-icad"
            telemetry["build"] = "0-mock"
        if intent.command == "GetModelFingerprint":
            telemetry["modelFingerprint"] = self._model_fingerprint
        return BridgeExecutionResult(
            success=True,
            stdout=f"mock:{intent.command}",
            telemetry=telemetry,
        )


class HttpBridgeClient:
    """HTTP client for GET /v1/model-fingerprint and POST /v1/execute."""

    def __init__(self, base_url: str, timeout_sec: float) -> None:
        self._base = base_url.rstrip("/")
        self._timeout = timeout_sec

    def get_model_fingerprint(self) -> str:
        url = f"{self._base}/v1/model-fingerprint"
        req = urllib.request.Request(url, method="GET")
        try:
            with urllib.request.urlopen(req, timeout=self._timeout) as resp:
                payload = json.loads(resp.read().decode())
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"model-fingerprint HTTP {exc.code}: {exc.read().decode()}") from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"model-fingerprint connection failed: {exc}") from exc
        fp = payload.get("modelFingerprint")
        if not isinstance(fp, str) or not fp:
            raise RuntimeError("model-fingerprint response missing modelFingerprint string")
        return fp

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult:
        url = f"{self._base}/v1/execute"
        body = json.dumps(intent.model_dump(mode="json")).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=body,
            method="POST",
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=self._timeout) as resp:
                payload = json.loads(resp.read().decode())
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode()
            return BridgeExecutionResult(
                success=False,
                stdout="",
                error_traceback=f"HTTP {exc.code}: {raw}",
                telemetry={"intentId": intent.intentId, "command": intent.command},
            )
        except urllib.error.URLError as exc:
            return BridgeExecutionResult(
                success=False,
                stdout="",
                error_traceback=f"connection failed: {exc}",
                telemetry={"intentId": intent.intentId, "command": intent.command},
            )
        return BridgeExecutionResult.model_validate(payload)


def create_bridge_client(config: OrchestratorConfig) -> BridgeClient:
    if config.bridge.mode == "mock":
        return MockBridgeClient(config.bridge.mock_model_fingerprint)
    return HttpBridgeClient(config.bridge.http_base_url, config.bridge.timeout_sec)
