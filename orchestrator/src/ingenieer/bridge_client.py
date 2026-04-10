"""CAD bridge clients: in-process mock and HTTP (loopback) per docs/BRIDGE_TRANSPORT.md."""

from __future__ import annotations

import errno
import http.client
import json
import random
import socket
import threading
import time
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from typing import Any, Protocol
from urllib.parse import SplitResult, urlsplit

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

        match intent.command:
            case "NoOp":
                telemetry["status"] = "nop_success"
            case "PingHost":
                telemetry["hostId"] = "mock-icad"
                telemetry["build"] = "0-mock"
            case "GetModelFingerprint":
                telemetry["modelFingerprint"] = self._fingerprint
            case "HighRiskStub":
                telemetry["stub_executed"] = True
            case "DrawPolylineFromCoordinates":
                telemetry["length"] = 123.45
                telemetry["point_count"] = len(intent.parameters.get("points", []))
            case "CreatePointBlocks":
                telemetry["point_count"] = len(intent.parameters.get("points", []))
            case "ImportLandXmlSurface":
                telemetry["surface_name"] = intent.parameters.get("surface_name", "unknown")
                telemetry["point_count"] = 500
                telemetry["triangle_count"] = 950
            case "VerifySurface":
                telemetry["point_count"] = 1024
                telemetry["triangle_count"] = 2000
                telemetry["bounds"] = [[0.0, 0.0, 0.0], [1000.0, 1000.0, 50.0]]
            case "CreateAlignment":
                telemetry["length"] = 538.52
                telemetry["station_range"] = [0.0, 538.52]
            case "CreateProfile":
                telemetry["pvi_count"] = 3
                telemetry["elevation_range"] = [100.0, 105.0]
            case "CreateCrossSection":
                telemetry["station_count"] = 5
            case "CreateCorridorModel":
                telemetry["corridor_length"] = 538.52
            case "BalanceGrading":
                telemetry["cut_volume"] = 1250.0
                telemetry["fill_volume"] = 1245.0
                telemetry["net_volume"] = 5.0
                telemetry["balanced"] = True
            case "CreateRetentionPond":
                telemetry["pond_volume"] = 4500.0
                telemetry["surface_area"] = 12000.0
            case "CreateSanitarySewerNetwork":
                telemetry["structure_count"] = 2
                telemetry["total_pipe_length"] = 250.0
            case "AnalyzeStormDrainage":
                telemetry["peak_discharge"] = 12.5
                telemetry["max_velocity"] = 4.2
                telemetry["capacity_exceeded"] = False
            case "PlacePlantingLayout":
                telemetry["plant_count"] = 2
                telemetry["canopy_coverage_area"] = 2513.27
            case "CreatePavingArea":
                telemetry["paving_area"] = 2500.0
                telemetry["perimeter_length"] = 200.0
            case "DesignIrrigationZone":
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


class _HttpTransportError(RuntimeError):
    def __init__(self, message: str, *, transient: bool, terminal: bool = False) -> None:
        super().__init__(message)
        self.transient = transient
        self.terminal = terminal


class _HttpConnectionPool:
    """Small deterministic keep-alive pool for the sequential orchestrator client."""

    def __init__(self, parsed_base: SplitResult, timeout_sec: float) -> None:
        self._parsed_base = parsed_base
        self._timeout = timeout_sec
        self._lock = threading.Lock()
        self._connection: http.client.HTTPConnection | http.client.HTTPSConnection | None = None

    def request(
        self,
        method: str,
        target: str,
        *,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
        timeout_sec: float | None = None,
    ) -> tuple[int, str, dict[str, str]]:
        with self._lock:
            request_timeout = self._timeout if timeout_sec is None else timeout_sec
            conn = self._connection
            if conn is None:
                conn = self._new_connection(request_timeout)
                self._connection = conn
            else:
                conn.timeout = request_timeout
                if conn.sock is not None:
                    conn.sock.settimeout(request_timeout)

            try:
                conn.request(method, target, body=body, headers=headers or {})
                response = conn.getresponse()
                raw = response.read().decode("utf-8")
                status = response.status
                response_headers = {key.lower(): value for key, value in response.getheaders()}
                if response.will_close:
                    self._close_locked()
                return status, raw, response_headers
            except Exception:
                self._close_locked()
                raise

    def close(self) -> None:
        with self._lock:
            self._close_locked()

    def _new_connection(self, timeout_sec: float) -> http.client.HTTPConnection | http.client.HTTPSConnection:
        host = self._parsed_base.hostname
        assert host is not None
        port = self._parsed_base.port
        if self._parsed_base.scheme == "https":
            return http.client.HTTPSConnection(host, port=port, timeout=timeout_sec)
        return http.client.HTTPConnection(host, port=port, timeout=timeout_sec)

    def _close_locked(self) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None


def _is_transient_transport_exception(exc: Exception) -> bool:
    if isinstance(
        exc,
        (
            TimeoutError,
            socket.timeout,
            BrokenPipeError,
            ConnectionAbortedError,
            ConnectionRefusedError,
            ConnectionResetError,
            http.client.BadStatusLine,
            http.client.CannotSendRequest,
            http.client.RemoteDisconnected,
            http.client.ResponseNotReady,
        ),
    ):
        return True
    if isinstance(exc, OSError):
        return exc.errno in {
            errno.ECONNABORTED,
            errno.ECONNREFUSED,
            errno.ECONNRESET,
            errno.EHOSTUNREACH,
            errno.ENETUNREACH,
            errno.EPIPE,
            errno.ETIMEDOUT,
        }
    return False


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _parse_retry_after(value: str | None) -> float | None:
    if value is None:
        return None

    stripped = value.strip()
    if not stripped:
        return None

    try:
        return max(0.0, float(int(stripped)))
    except ValueError:
        pass

    try:
        retry_dt = parsedate_to_datetime(stripped)
    except (TypeError, ValueError, IndexError):
        return None

    if retry_dt.tzinfo is None:
        retry_dt = retry_dt.replace(tzinfo=timezone.utc)

    return max(0.0, (retry_dt.astimezone(timezone.utc) - _utc_now()).total_seconds())


class HttpBridgeClient:
    """HTTP client for GET /v1/model-fingerprint and POST /v1/execute."""

    def __init__(
        self,
        base_url: str,
        timeout_sec: float,
        *,
        deadline_sec: float = 60.0,
        max_retries: int = 2,
        retry_backoff_sec: float = 0.25,
    ) -> None:
        parsed_base = urlsplit(base_url.rstrip("/"))
        if parsed_base.scheme not in {"http", "https"}:
            raise ValueError(f"Unsupported bridge URL scheme: {parsed_base.scheme!r}")
        if parsed_base.hostname is None:
            raise ValueError(f"Bridge base URL must include a host: {base_url!r}")

        self._base = parsed_base
        self._timeout = timeout_sec
        self._deadline = deadline_sec
        self._max_retries = max_retries
        self._retry_backoff = retry_backoff_sec
        self._pool = _HttpConnectionPool(parsed_base, timeout_sec)

    def _default_backoff_delay(self, attempt: int) -> float:
        base = self._retry_backoff * (2**attempt)
        jitter = random.uniform(0, self._retry_backoff * 0.5)
        return base + jitter

    def _sleep_for(self, delay_sec: float) -> None:
        time.sleep(delay_sec)

    def _deadline_error(self) -> _HttpTransportError:
        return _HttpTransportError(
            f"deadline exceeded after {self._deadline:.2f}s",
            transient=True,
            terminal=True,
        )

    def _sleep_with_deadline(self, delay_sec: float, deadline: float, *, cause: Exception | None = None) -> None:
        if time.monotonic() + delay_sec >= deadline:
            raise self._deadline_error() from cause
        self._sleep_for(delay_sec)

    def _build_request_headers(
        self,
        *,
        request_id_prefix: str,
        attempt_number: int,
        body: bytes | None = None,
        idempotency_key: str | None = None,
    ) -> dict[str, str]:
        headers = {
            "Accept": "application/json",
            "Connection": "keep-alive",
            "X-Request-Id": f"{request_id_prefix}:attempt-{attempt_number}",
        }
        if body is not None:
            headers["Content-Type"] = "application/json"
        if idempotency_key is not None:
            headers["X-Idempotency-Key"] = idempotency_key
        return headers

    def _retry_delay_for_response(
        self,
        status_code: int,
        retry_after: str | None,
        attempt: int,
    ) -> float:
        if status_code in (429, 503):
            retry_after_delay = _parse_retry_after(retry_after)
            if retry_after_delay is not None:
                return retry_after_delay
        return self._default_backoff_delay(attempt)

    def _open_json(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        attempts: int | None = None,
        request_id_prefix: str,
        idempotency_key: str | None = None,
    ) -> tuple[int, dict[str, Any]]:
        last_exc: _HttpTransportError | None = None
        tries = (self._max_retries + 1) if attempts is None else attempts
        deadline = time.monotonic() + self._deadline
        target = self._build_target(path)

        for attempt in range(tries):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise self._deadline_error()

            try:
                request_timeout = min(self._timeout, remaining)
                headers = self._build_request_headers(
                    request_id_prefix=request_id_prefix,
                    attempt_number=attempt + 1,
                    body=body,
                    idempotency_key=idempotency_key,
                )
                code, raw, response_headers = self._pool.request(
                    method,
                    target,
                    body=body,
                    headers=headers,
                    timeout_sec=request_timeout,
                )
                if _transient_http_code(code):
                    last_exc = _HttpTransportError(f"HTTP {code}: {raw}", transient=True)
                    self._pool.close()
                    if attempt + 1 < tries:
                        backoff = self._retry_delay_for_response(
                            code,
                            response_headers.get("retry-after"),
                            attempt,
                        )
                        self._sleep_with_deadline(backoff, deadline)
                        continue
                    raise last_exc
                if not 200 <= code < 300:
                    raise _HttpTransportError(f"HTTP {code}: {raw}", transient=False)
                try:
                    return code, json.loads(raw)
                except json.JSONDecodeError as exc:
                    raise _HttpTransportError(
                        f"invalid JSON response: {exc.msg}",
                        transient=False,
                    ) from exc
            except _HttpTransportError as exc:
                last_exc = exc
                if exc.transient and not exc.terminal and attempt + 1 < tries:
                    backoff = self._default_backoff_delay(attempt)
                    self._sleep_with_deadline(backoff, deadline, cause=exc)
                    continue
                raise
            except Exception as exc:
                message = (
                    f"timeout after {self._timeout:.2f}s"
                    if isinstance(exc, (TimeoutError, socket.timeout))
                    else f"connection failed: {exc}"
                )
                last_exc = _HttpTransportError(
                    message,
                    transient=_is_transient_transport_exception(exc),
                )
                if last_exc.transient and attempt + 1 < tries:
                    backoff = self._default_backoff_delay(attempt)
                    self._sleep_with_deadline(backoff, deadline, cause=exc)
                    continue
                raise last_exc from exc

        assert last_exc is not None
        raise last_exc

    def _build_target(self, path: str) -> str:
        base_path = self._base.path.rstrip("/")
        return f"{base_path}{path}" if base_path else path

    def close(self) -> None:
        self._pool.close()

    def _get_model_fingerprint(self, request_id_prefix: str) -> str:
        try:
            _code, payload = self._open_json(
                "GET",
                "/v1/model-fingerprint",
                request_id_prefix=request_id_prefix,
            )
        except _HttpTransportError as exc:
            raise _HttpTransportError(
                f"model-fingerprint {exc}",
                transient=exc.transient,
            ) from exc
        fp = payload.get("modelFingerprint")
        if not isinstance(fp, str) or not fp:
            raise RuntimeError("model-fingerprint response missing modelFingerprint string")
        return fp

    def get_model_fingerprint(self) -> str:
        return self._get_model_fingerprint("model-fingerprint")

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult:
        body = json.dumps(intent.model_dump(mode="json")).encode("utf-8")
        try:
            _code, payload = self._open_json(
                "POST",
                "/v1/execute",
                body=body,
                request_id_prefix=intent.intentId,
                idempotency_key=intent.intentId,
            )
        except _HttpTransportError as exc:
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
            observed = self._get_model_fingerprint(f"verify-fingerprint:{intent.intentId}")
        except _HttpTransportError as exc:
            return BridgeVerifyResult(
                success=False,
                transient_failure=exc.transient,
                message=f"verify fingerprint read failed: {exc}",
                evidence={"intentId": intent.intentId},
            )
        except Exception as exc:  # noqa: BLE001 — fail closed for non-transport protocol issues
            return BridgeVerifyResult(
                success=False,
                transient_failure=False,
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
        deadline_sec=config.bridge.deadline_sec,
        max_retries=config.bridge.http_max_retries,
        retry_backoff_sec=config.bridge.http_retry_backoff_sec,
    )
