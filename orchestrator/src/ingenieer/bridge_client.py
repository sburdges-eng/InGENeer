"""CAD bridge clients: in-process mock and HTTP (loopback) per docs/BRIDGE_TRANSPORT.md."""

from __future__ import annotations

import json
import random
import time
import urllib.error
import urllib.request
from typing import Any, Protocol

from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.wire import BridgeExecutionResult, BridgeVerifyResult


class BridgeClient(Protocol):
    def get_model_fingerprint(self) -> str: ...

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult: ...

    def verify_post_dispatch(
        self,
        intent: CadIntentEnvelope,
        dispatch_telemetry: dict[str, Any],
    ) -> BridgeVerifyResult: ...


def _telemetry_scalar_str(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, str):
        return value
    if isinstance(value, (int, float, bool)):
        return str(value)
    return None


class MockBridgeClient:
    """Deterministic bridge for tests and CI (no network)."""

    def __init__(
        self,
        model_fingerprint: str = "stub-fingerprint",
        *,
        verify_transient_failures: int = 0,
    ) -> None:
        self._fingerprint = model_fingerprint
        self._mutation_seq = 0
        self._verify_transient_remaining = verify_transient_failures

    def get_model_fingerprint(self) -> str:
        return self._fingerprint

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult:
        if intent.parameters.get("_bridge_execute_fail") is True:
            return BridgeExecutionResult(
                success=False,
                stdout="",
                error_traceback="mock failure (_bridge_execute_fail)",
                telemetry={"intentId": intent.intentId, "command": intent.command},
            )
        telemetry: dict[str, object] = {
            "intentId": intent.intentId,
            "command": intent.command,
            "executionMode": intent.executionMode,
        }
        fp_before = self._fingerprint
        if intent.executionMode in ("dry_run", "preview"):
            telemetry["modelFingerprintAfter"] = fp_before
            telemetry["plannedSummary"] = f"mock:{intent.command}:{intent.executionMode}"
            stdout = f"mock:{intent.command}:{intent.executionMode}"
        else:
            self._mutation_seq += 1
            self._fingerprint = f"{fp_before}#m{self._mutation_seq}"
            telemetry["modelFingerprintAfter"] = self._fingerprint
            telemetry["plannedSummary"] = ""
            stdout = f"mock:{intent.command}"

        if intent.command == "PingHost":
            telemetry["hostId"] = "mock-icad"
            telemetry["build"] = "0-mock"
        if intent.command == "GetModelFingerprint":
            telemetry["modelFingerprint"] = self._fingerprint
        if intent.command == "VerifySurface":
            telemetry["point_count"] = 1024
            telemetry["triangle_count"] = 2000
            telemetry["bounds"] = [[0.0, 0.0, 0.0], [1000.0, 1000.0, 50.0]]
        if intent.command == "CreateAlignment":
            telemetry["length"] = 538.52
            telemetry["station_range"] = [0.0, 538.52]
        if intent.command == "CreateProfile":
            telemetry["pvi_count"] = 3
            telemetry["elevation_range"] = [100.0, 105.0]
        if intent.command == "CreateCrossSection":
            telemetry["station_count"] = 5
        if intent.command == "CreateCorridorModel":
            telemetry["corridor_length"] = 538.52
        if intent.command == "BalanceGrading":
            telemetry["cut_volume"] = 1250.0
            telemetry["fill_volume"] = 1245.0
            telemetry["net_volume"] = 5.0
            telemetry["balanced"] = True
        if intent.command == "CreateRetentionPond":
            telemetry["pond_volume"] = 4500.0
            telemetry["surface_area"] = 12000.0
        if intent.command == "CreateSanitarySewerNetwork":
            telemetry["structure_count"] = 2
            telemetry["total_pipe_length"] = 250.0
        if intent.command == "AnalyzeStormDrainage":
            telemetry["peak_discharge"] = 12.5
            telemetry["max_velocity"] = 4.2
            telemetry["capacity_exceeded"] = False
        if intent.command == "PlacePlantingLayout":
            telemetry["plant_count"] = 2
            telemetry["canopy_coverage_area"] = 2513.27
        if intent.command == "CreatePavingArea":
            telemetry["paving_area"] = 2500.0
            telemetry["perimeter_length"] = 200.0
        if intent.command == "DesignIrrigationZone":
            telemetry["head_count"] = 2
            telemetry["total_flow_gpm"] = 12.4
            telemetry["pipe_length"] = 35.0
        return BridgeExecutionResult(
            success=True,
            stdout=stdout,
            telemetry=telemetry,
        )

    def verify_post_dispatch(
        self,
        intent: CadIntentEnvelope,
        dispatch_telemetry: dict[str, Any],
    ) -> BridgeVerifyResult:
        if self._verify_transient_remaining > 0:
            self._verify_transient_remaining -= 1
            return BridgeVerifyResult(
                success=False,
                transient_failure=True,
                message="mock transient verify failure",
                evidence={"intentId": intent.intentId},
            )
        expected = _telemetry_scalar_str(dispatch_telemetry.get("modelFingerprintAfter"))
        if not expected:
            return BridgeVerifyResult(
                success=False,
                message="dispatch telemetry missing modelFingerprintAfter (host must publish post-state)",
                evidence={"intentId": intent.intentId},
            )
        observed = self.get_model_fingerprint()
        if observed != expected:
            return BridgeVerifyResult(
                success=False,
                message="model fingerprint mismatch: live host state does not match dispatch telemetry",
                observed_fingerprint=observed,
                expected_fingerprint=expected,
                evidence={"intentId": intent.intentId},
            )
        return BridgeVerifyResult(
            success=True,
            message="fingerprint consistent with dispatch telemetry",
            observed_fingerprint=observed,
            expected_fingerprint=expected,
            evidence={"intentId": intent.intentId},
        )


def _transient_http_code(code: int) -> bool:
    return code in (429, 502, 503, 504)


class HttpBridgeClient:
    """HTTP client for GET /v1/model-fingerprint and POST /v1/execute."""

    def __init__(
        self,
        base_url: str,
        timeout_sec: float,
        *,
        max_retries: int = 2,
        retry_backoff_sec: float = 0.25,
    ) -> None:
        self._base = base_url.rstrip("/")
        self._timeout = timeout_sec
        self._max_retries = max_retries
        self._retry_backoff = retry_backoff_sec

    def _sleep_backoff(self, attempt: int) -> None:
        base = self._retry_backoff * (2**attempt)
        jitter = random.uniform(0, self._retry_backoff * 0.5)
        time.sleep(base + jitter)

    def _open_json(
        self,
        req: urllib.request.Request,
        *,
        attempts: int | None = None,
    ) -> tuple[int, dict[str, Any]]:
        last_exc: Exception | None = None
        tries = (self._max_retries + 1) if attempts is None else attempts
        for attempt in range(tries):
            try:
                with urllib.request.urlopen(req, timeout=self._timeout) as resp:
                    raw = resp.read().decode()
                    code = resp.getcode()
                    return code, json.loads(raw)
            except urllib.error.HTTPError as exc:
                raw = exc.read().decode()
                if _transient_http_code(exc.code) and attempt + 1 < tries:
                    self._sleep_backoff(attempt)
                    continue
                raise RuntimeError(f"HTTP {exc.code}: {raw}") from exc
            except urllib.error.URLError as exc:
                last_exc = exc
                if attempt + 1 < tries:
                    self._sleep_backoff(attempt)
                    continue
                raise RuntimeError(f"connection failed: {exc}") from exc
        assert last_exc is not None
        raise RuntimeError(f"connection failed: {last_exc}") from last_exc

    def get_model_fingerprint(self) -> str:
        url = f"{self._base}/v1/model-fingerprint"
        req = urllib.request.Request(url, method="GET")
        try:
            _code, payload = self._open_json(req)
        except RuntimeError as exc:
            raise RuntimeError(f"model-fingerprint {exc}") from exc
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
            _code, payload = self._open_json(req)
        except RuntimeError as exc:
            return BridgeExecutionResult(
                success=False,
                stdout="",
                error_traceback=str(exc),
                telemetry={"intentId": intent.intentId, "command": intent.command},
            )
        return BridgeExecutionResult.model_validate(payload)

    def verify_post_dispatch(
        self,
        intent: CadIntentEnvelope,
        dispatch_telemetry: dict[str, Any],
    ) -> BridgeVerifyResult:
        expected = _telemetry_scalar_str(dispatch_telemetry.get("modelFingerprintAfter"))
        if not expected:
            return BridgeVerifyResult(
                success=False,
                message="dispatch telemetry missing modelFingerprintAfter",
                evidence={"intentId": intent.intentId},
            )
        try:
            observed = self.get_model_fingerprint()
        except Exception as exc:  # noqa: BLE001 — classify transient at HTTP layer
            err = str(exc).lower()
            transient = "connection" in err or "timeout" in err or "503" in err or "502" in err
            return BridgeVerifyResult(
                success=False,
                transient_failure=transient,
                message=f"verify fingerprint read failed: {exc}",
                evidence={"intentId": intent.intentId},
            )
        if observed != expected:
            return BridgeVerifyResult(
                success=False,
                message="model fingerprint mismatch after dispatch",
                observed_fingerprint=observed,
                expected_fingerprint=expected,
                evidence={"intentId": intent.intentId},
            )
        return BridgeVerifyResult(
            success=True,
            message="fingerprint consistent with dispatch telemetry",
            observed_fingerprint=observed,
            expected_fingerprint=expected,
            evidence={"intentId": intent.intentId},
        )


def create_bridge_client(config: OrchestratorConfig) -> BridgeClient:
    if config.bridge.mode == "mock":
        return MockBridgeClient(config.bridge.mock_model_fingerprint)
    return HttpBridgeClient(
        config.bridge.http_base_url,
        config.bridge.timeout_sec,
        max_retries=config.bridge.http_max_retries,
        retry_backoff_sec=config.bridge.http_retry_backoff_sec,
    )
