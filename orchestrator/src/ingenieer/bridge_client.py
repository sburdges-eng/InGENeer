"""CAD bridge clients: in-process mock and HTTP (loopback) per docs/BRIDGE_TRANSPORT.md."""

from __future__ import annotations

import errno
import http.client
import json
import random
import socket
import threading
import time
from dataclasses import dataclass, field
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


@dataclass(slots=True)
class TransportTelemetry:
    request_id: str
    attempts: int
    total_duration_sec: float
    retried_status_codes: list[int] = field(default_factory=list)
    retry_after_used: bool = False
    final_status_code: int | None = None
    error_class: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return {
            "request_id": self.request_id,
            "attempts": self.attempts,
            "total_duration_sec": self.total_duration_sec,
            "retried_status_codes": list(self.retried_status_codes),
            "retry_after_used": self.retry_after_used,
            "final_status_code": self.final_status_code,
            "error_class": self.error_class,
        }


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
        self.last_transport_telemetry: TransportTelemetry | None = None

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
    def __init__(
        self,
        message: str,
        *,
        transient: bool,
        terminal: bool = False,
        error_class: str | None = None,
    ) -> None:
        super().__init__(message)
        self.transient = transient
        self.terminal = terminal
        self.error_class = error_class
        self.transport_telemetry: TransportTelemetry | None = None


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
        self.last_transport_telemetry: TransportTelemetry | None = None

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
            error_class="timeout",
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
    ) -> tuple[float, bool]:
        if status_code in (429, 503):
            retry_after_delay = _parse_retry_after(retry_after)
            if retry_after_delay is not None:
                return retry_after_delay, True
        return self._default_backoff_delay(attempt), False

    def _build_transport_telemetry(
        self,
        *,
        request_id: str,
        attempts: int,
        started_at: float,
        retried_status_codes: list[int],
        retry_after_used: bool,
        final_status_code: int | None,
        error_class: str | None,
    ) -> TransportTelemetry:
        return TransportTelemetry(
            request_id=request_id,
            attempts=attempts,
            total_duration_sec=max(0.0, time.monotonic() - started_at),
            retried_status_codes=list(retried_status_codes),
            retry_after_used=retry_after_used,
            final_status_code=final_status_code,
            error_class=error_class,
        )

    def _attach_transport_telemetry(
        self,
        exc: _HttpTransportError,
        *,
        request_id: str,
        attempts: int,
        started_at: float,
        retried_status_codes: list[int],
        retry_after_used: bool,
        final_status_code: int | None,
        error_class: str | None,
    ) -> _HttpTransportError:
        telemetry = self._build_transport_telemetry(
            request_id=request_id,
            attempts=attempts,
            started_at=started_at,
            retried_status_codes=retried_status_codes,
            retry_after_used=retry_after_used,
            final_status_code=final_status_code,
            error_class=error_class,
        )
        exc.transport_telemetry = telemetry
        self.last_transport_telemetry = telemetry
        return exc

    def _open_json(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        attempts: int | None = None,
        request_id_prefix: str,
        idempotency_key: str | None = None,
    ) -> tuple[int, dict[str, Any], TransportTelemetry]:
        last_exc: _HttpTransportError | None = None
        tries = (self._max_retries + 1) if attempts is None else attempts
        deadline = time.monotonic() + self._deadline
        started_at = time.monotonic()
        target = self._build_target(path)
        retried_status_codes: list[int] = []
        retry_after_used = False
        last_request_id = ""

        for attempt in range(tries):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise self._attach_transport_telemetry(
                    self._deadline_error(),
                    request_id=last_request_id,
                    attempts=attempt,
                    started_at=started_at,
                    retried_status_codes=retried_status_codes,
                    retry_after_used=retry_after_used,
                    final_status_code=None,
                    error_class="timeout",
                )

            try:
                request_timeout = min(self._timeout, remaining)
                headers = self._build_request_headers(
                    request_id_prefix=request_id_prefix,
                    attempt_number=attempt + 1,
                    body=body,
                    idempotency_key=idempotency_key,
                )
                last_request_id = headers["X-Request-Id"]
                code, raw, response_headers = self._pool.request(
                    method,
                    target,
                    body=body,
                    headers=headers,
                    timeout_sec=request_timeout,
                )
                if _transient_http_code(code):
                    last_exc = _HttpTransportError(
                        f"HTTP {code}: {raw}",
                        transient=True,
                        error_class="transient",
                    )
                    self._pool.close()
                    if attempt + 1 < tries:
                        retried_status_codes.append(code)
                        backoff, used_retry_after = self._retry_delay_for_response(
                            code,
                            response_headers.get("retry-after"),
                            attempt,
                        )
                        retry_after_used = retry_after_used or used_retry_after
                        self._sleep_with_deadline(backoff, deadline)
                        continue
                    raise self._attach_transport_telemetry(
                        last_exc,
                        request_id=last_request_id,
                        attempts=attempt + 1,
                        started_at=started_at,
                        retried_status_codes=retried_status_codes,
                        retry_after_used=retry_after_used,
                        final_status_code=code,
                        error_class="transient",
                    )
                if not 200 <= code < 300:
                    raise self._attach_transport_telemetry(
                        _HttpTransportError(
                            f"HTTP {code}: {raw}",
                            transient=False,
                            error_class="permanent",
                        ),
                        request_id=last_request_id,
                        attempts=attempt + 1,
                        started_at=started_at,
                        retried_status_codes=retried_status_codes,
                        retry_after_used=retry_after_used,
                        final_status_code=code,
                        error_class="permanent",
                    )
                try:
                    payload = json.loads(raw)
                    transport = self._build_transport_telemetry(
                        request_id=last_request_id,
                        attempts=attempt + 1,
                        started_at=started_at,
                        retried_status_codes=retried_status_codes,
                        retry_after_used=retry_after_used,
                        final_status_code=code,
                        error_class=None,
                    )
                    self.last_transport_telemetry = transport
                    return code, payload, transport
                except json.JSONDecodeError as exc:
                    raise self._attach_transport_telemetry(
                        _HttpTransportError(
                            f"invalid JSON response: {exc.msg}",
                            transient=False,
                            error_class="permanent",
                        ),
                        request_id=last_request_id,
                        attempts=attempt + 1,
                        started_at=started_at,
                        retried_status_codes=retried_status_codes,
                        retry_after_used=retry_after_used,
                        final_status_code=code,
                        error_class="permanent",
                    ) from exc
            except _HttpTransportError as exc:
                if exc.transport_telemetry is None:
                    error_class = exc.error_class or ("transient" if exc.transient else "permanent")
                    exc = self._attach_transport_telemetry(
                        exc,
                        request_id=last_request_id,
                        attempts=attempt + 1,
                        started_at=started_at,
                        retried_status_codes=retried_status_codes,
                        retry_after_used=retry_after_used,
                        final_status_code=None,
                        error_class=error_class,
                    )
                last_exc = exc
                if exc.transient and not exc.terminal and attempt + 1 < tries:
                    backoff = self._default_backoff_delay(attempt)
                    self._sleep_with_deadline(backoff, deadline, cause=exc)
                    continue
                raise
            except Exception as exc:
                error_class = (
                    "timeout"
                    if isinstance(exc, (TimeoutError, socket.timeout))
                    else "transient" if _is_transient_transport_exception(exc) else "permanent"
                )
                message = (
                    f"timeout after {self._timeout:.2f}s"
                    if isinstance(exc, (TimeoutError, socket.timeout))
                    else f"connection failed: {exc}"
                )
                last_exc = _HttpTransportError(
                    message,
                    transient=_is_transient_transport_exception(exc),
                    error_class=error_class,
                )
                if last_exc.transient and attempt + 1 < tries:
                    backoff = self._default_backoff_delay(attempt)
                    self._sleep_with_deadline(backoff, deadline, cause=exc)
                    continue
                raise self._attach_transport_telemetry(
                    last_exc,
                    request_id=last_request_id,
                    attempts=attempt + 1,
                    started_at=started_at,
                    retried_status_codes=retried_status_codes,
                    retry_after_used=retry_after_used,
                    final_status_code=None,
                    error_class=error_class,
                ) from exc

        assert last_exc is not None
        raise last_exc

    def _build_target(self, path: str) -> str:
        base_path = self._base.path.rstrip("/")
        return f"{base_path}{path}" if base_path else path

    def close(self) -> None:
        self._pool.close()

    def _get_model_fingerprint(self, request_id_prefix: str) -> str:
        try:
            _code, payload, _transport = self._open_json(
                "GET",
                "/v1/model-fingerprint",
                request_id_prefix=request_id_prefix,
            )
        except _HttpTransportError as exc:
            wrapped = _HttpTransportError(
                f"model-fingerprint {exc}",
                transient=exc.transient,
                terminal=exc.terminal,
                error_class=exc.error_class,
            )
            wrapped.transport_telemetry = exc.transport_telemetry
            self.last_transport_telemetry = exc.transport_telemetry
            raise wrapped from exc
        fp = payload.get("modelFingerprint")
        if not isinstance(fp, str) or not fp:
            raise RuntimeError("model-fingerprint response missing modelFingerprint string")
        return fp

    def get_model_fingerprint(self) -> str:
        return self._get_model_fingerprint("model-fingerprint")

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult:
        body = json.dumps(intent.model_dump(mode="json")).encode("utf-8")
        try:
            _code, payload, _transport = self._open_json(
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
