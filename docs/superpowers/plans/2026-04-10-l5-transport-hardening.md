# L5 Transport Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the HTTP bridge client (`HttpBridgeClient`) with proper error classification, per-request timeouts, connection reuse, and comprehensive edge-case tests.

**Architecture:** The bridge client at `orchestrator/src/ingenieer/bridge_client.py` currently uses raw `urllib.request` with basic retry logic. We'll add: (1) a `BridgeError` hierarchy that classifies transient vs permanent failures, (2) per-request deadline enforcement distinct from socket timeout, (3) `urllib3`-based connection pooling with keep-alive, and (4) a test harness using a real `http.server` to simulate slow/broken responses.

**Tech Stack:** Python 3.11+, urllib3 (new dep), http.server (stdlib, tests only), pydantic, pytest

---

## File Structure

| File | Responsibility |
|------|----------------|
| `orchestrator/src/ingenieer/bridge_errors.py` | **Create.** `BridgeError` base, `BridgeTransientError`, `BridgePermanentError`, `BridgeTimeoutError` hierarchy. Classification helper `classify_http_error()`. |
| `orchestrator/src/ingenieer/bridge_client.py` | **Modify.** Replace `urllib.request` in `HttpBridgeClient` with `urllib3.PoolManager`. Use `BridgeError` hierarchy. Add `deadline_sec` field to `BridgeConfig`. |
| `orchestrator/src/ingenieer/models.py` | **Modify.** Add `deadline_sec` to `BridgeConfig` (per-pipeline deadline, distinct from per-socket `timeout_sec`). |
| `orchestrator/tests/test_bridge_errors.py` | **Create.** Unit tests for error classification logic. |
| `orchestrator/tests/test_bridge_http.py` | **Create.** Integration-style tests using a local `http.server` that simulates: slow response, connection refused, malformed JSON, 503 then 200, partial read, 400 permanent error. |
| `orchestrator/tests/test_bridge_client.py` | **Modify.** Add tests for deadline enforcement and urllib3 pooling config. |
| `orchestrator/pyproject.toml` | **Modify.** Add `urllib3>=2.0` to dependencies. |

---

### Task 1: Error Hierarchy

**Files:**
- Create: `orchestrator/src/ingenieer/bridge_errors.py`
- Create: `orchestrator/tests/test_bridge_errors.py`

- [ ] **Step 1: Write failing tests for error classification**

```python
# orchestrator/tests/test_bridge_errors.py
from ingenieer.bridge_errors import (
    BridgeError,
    BridgePermanentError,
    BridgeTimeoutError,
    BridgeTransientError,
    classify_http_status,
)


def test_503_is_transient():
    err = classify_http_status(503, "service unavailable")
    assert isinstance(err, BridgeTransientError)
    assert err.status_code == 503


def test_502_is_transient():
    err = classify_http_status(502, "bad gateway")
    assert isinstance(err, BridgeTransientError)


def test_429_is_transient():
    err = classify_http_status(429, "too many requests")
    assert isinstance(err, BridgeTransientError)


def test_504_is_transient():
    err = classify_http_status(504, "gateway timeout")
    assert isinstance(err, BridgeTransientError)


def test_400_is_permanent():
    err = classify_http_status(400, "bad request")
    assert isinstance(err, BridgePermanentError)
    assert err.status_code == 400


def test_404_is_permanent():
    err = classify_http_status(404, "not found")
    assert isinstance(err, BridgePermanentError)


def test_500_is_permanent():
    err = classify_http_status(500, "internal server error")
    assert isinstance(err, BridgePermanentError)


def test_all_errors_are_bridge_error():
    for code in (400, 429, 500, 502, 503, 504):
        err = classify_http_status(code, "msg")
        assert isinstance(err, BridgeError)


def test_timeout_error_is_transient():
    err = BridgeTimeoutError("timed out after 5s")
    assert isinstance(err, BridgeTransientError)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd orchestrator && python3 -m pytest tests/test_bridge_errors.py -q`
Expected: FAIL — module not found

- [ ] **Step 3: Implement error hierarchy**

```python
# orchestrator/src/ingenieer/bridge_errors.py
"""Bridge error hierarchy: transient vs permanent for retry decisions."""

from __future__ import annotations

_TRANSIENT_CODES = frozenset({429, 502, 503, 504})


class BridgeError(Exception):
    """Base for all bridge transport errors."""

    def __init__(self, message: str, *, status_code: int | None = None) -> None:
        super().__init__(message)
        self.status_code = status_code


class BridgeTransientError(BridgeError):
    """Retryable transport failure (connection reset, 503, 429, timeout)."""


class BridgePermanentError(BridgeError):
    """Non-retryable failure (400, 404, 500, malformed response)."""


class BridgeTimeoutError(BridgeTransientError):
    """Request exceeded deadline or socket timeout."""


def classify_http_status(code: int, body: str) -> BridgeError:
    """Return the appropriate BridgeError subclass for an HTTP status code."""
    msg = f"HTTP {code}: {body}"
    if code in _TRANSIENT_CODES:
        return BridgeTransientError(msg, status_code=code)
    return BridgePermanentError(msg, status_code=code)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd orchestrator && python3 -m pytest tests/test_bridge_errors.py -q`
Expected: 10 passed

- [ ] **Step 5: Lint and commit**

```bash
cd orchestrator && ruff check src/ingenieer/bridge_errors.py tests/test_bridge_errors.py
git add orchestrator/src/ingenieer/bridge_errors.py orchestrator/tests/test_bridge_errors.py
git commit -m "feat(L5): add BridgeError hierarchy for retry classification"
```

---

### Task 2: Add urllib3 Dependency and deadline_sec Config

**Files:**
- Modify: `orchestrator/pyproject.toml` (add urllib3)
- Modify: `orchestrator/src/ingenieer/models.py:27-36` (add `deadline_sec` to `BridgeConfig`)
- Modify: `orchestrator/tests/test_config_loaders.py` (verify deadline_sec defaults)

- [ ] **Step 1: Write failing test for deadline_sec default**

Add to `orchestrator/tests/test_config_loaders.py`:

```python
def test_bridge_config_deadline_sec_default():
    from ingenieer.models import BridgeConfig

    cfg = BridgeConfig()
    assert cfg.deadline_sec == 60.0
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd orchestrator && python3 -m pytest tests/test_config_loaders.py::test_bridge_config_deadline_sec_default -q`
Expected: FAIL — no attribute `deadline_sec`

- [ ] **Step 3: Add deadline_sec to BridgeConfig and urllib3 dep**

In `orchestrator/src/ingenieer/models.py`, add to `BridgeConfig` after `http_retry_backoff_sec`:

```python
    # Per-pipeline deadline: total wall-clock limit for a single bridge call
    # (distinct from timeout_sec which is the per-socket read timeout).
    deadline_sec: float = Field(default=60.0, ge=1.0, le=600.0)
```

In `orchestrator/pyproject.toml`, add `urllib3>=2.0` to `dependencies`:

```toml
dependencies = [
    "pydantic>=2.5",
    "jsonschema>=4.20",
    "urllib3>=2.0",
]
```

- [ ] **Step 4: Install and verify**

Run:
```bash
cd orchestrator && pip install -e ".[dev]" && python3 -m pytest tests/test_config_loaders.py -q
```
Expected: all pass (including new test)

- [ ] **Step 5: Commit**

```bash
git add orchestrator/pyproject.toml orchestrator/src/ingenieer/models.py orchestrator/tests/test_config_loaders.py
git commit -m "feat(L5): add urllib3 dep and deadline_sec to BridgeConfig"
```

---

### Task 3: Replace urllib.request with urllib3 in HttpBridgeClient

**Files:**
- Modify: `orchestrator/src/ingenieer/bridge_client.py:180-315` (rewrite `HttpBridgeClient`)
- Modify: `orchestrator/tests/test_bridge_client.py` (update constructor test)

- [ ] **Step 1: Write failing test for pool manager existence**

Add to `orchestrator/tests/test_bridge_client.py`:

```python
def test_http_client_has_pool_manager():
    from ingenieer.bridge_client import HttpBridgeClient

    h = HttpBridgeClient("http://127.0.0.1:9999", timeout_sec=5.0, deadline_sec=30.0)
    assert hasattr(h, "_pool")
    # Pool manager should be a urllib3.PoolManager
    import urllib3
    assert isinstance(h._pool, urllib3.PoolManager)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd orchestrator && python3 -m pytest tests/test_bridge_client.py::test_http_client_has_pool_manager -q`
Expected: FAIL — no `_pool` attribute or wrong type

- [ ] **Step 3: Rewrite HttpBridgeClient with urllib3**

Replace the entire `HttpBridgeClient` class in `orchestrator/src/ingenieer/bridge_client.py` (lines 180–315). Key changes:

```python
import urllib3

from ingenieer.bridge_errors import (
    BridgePermanentError,
    BridgeTimeoutError,
    BridgeTransientError,
    classify_http_status,
)

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
        self._base = base_url.rstrip("/")
        self._timeout = timeout_sec
        self._deadline = deadline_sec
        self._max_retries = max_retries
        self._retry_backoff = retry_backoff_sec
        self._pool = urllib3.PoolManager(
            num_pools=2,  # fingerprint + execute endpoints
            maxsize=4,
            timeout=urllib3.Timeout(connect=5.0, read=timeout_sec),
            retries=False,  # we handle retries ourselves
        )

    def _request_json(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
    ) -> dict[str, Any]:
        url = f"{self._base}{path}"
        headers = {"Content-Type": "application/json"} if body else {}
        deadline = time.monotonic() + self._deadline
        last_exc: Exception | None = None

        for attempt in range(self._max_retries + 1):
            if time.monotonic() > deadline:
                raise BridgeTimeoutError(
                    f"deadline exceeded after {self._deadline}s ({attempt} attempts)"
                )
            try:
                resp = self._pool.request(
                    method, url, body=body, headers=headers
                )
                if 200 <= resp.status < 300:
                    return json.loads(resp.data.decode("utf-8"))
                err = classify_http_status(resp.status, resp.data.decode("utf-8", errors="replace"))
                if isinstance(err, BridgeTransientError) and attempt + 1 <= self._max_retries:
                    self._sleep_backoff(attempt)
                    last_exc = err
                    continue
                raise err
            except BridgeError:
                raise
            except urllib3.exceptions.TimeoutError as exc:
                if attempt + 1 <= self._max_retries and time.monotonic() < deadline:
                    self._sleep_backoff(attempt)
                    last_exc = exc
                    continue
                raise BridgeTimeoutError(f"socket timeout: {exc}") from exc
            except urllib3.exceptions.HTTPError as exc:
                if attempt + 1 <= self._max_retries and time.monotonic() < deadline:
                    self._sleep_backoff(attempt)
                    last_exc = exc
                    continue
                raise BridgeTransientError(f"connection failed: {exc}") from exc

        raise BridgeTransientError(f"exhausted {self._max_retries} retries: {last_exc}")

    def _sleep_backoff(self, attempt: int) -> None:
        base = self._retry_backoff * (2 ** attempt)
        jitter = random.uniform(0, self._retry_backoff * 0.5)
        time.sleep(base + jitter)

    def get_model_fingerprint(self) -> str:
        payload = self._request_json("GET", "/v1/model-fingerprint")
        fp = payload.get("modelFingerprint")
        if not isinstance(fp, str) or not fp:
            raise BridgePermanentError("model-fingerprint response missing modelFingerprint string")
        return fp

    def execute_intent(self, intent: CadIntentEnvelope) -> BridgeExecutionResult:
        body = json.dumps(intent.model_dump(mode="json")).encode("utf-8")
        try:
            payload = self._request_json("POST", "/v1/execute", body=body)
        except BridgeError as exc:
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
        except BridgeError as exc:
            transient = isinstance(exc, BridgeTransientError)
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
```

Also update `create_bridge_client()` to pass `deadline_sec`:

```python
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
```

Remove the old `import urllib.error`, `import urllib.request` imports. Keep `import json`, `import random`, `import time`.

- [ ] **Step 4: Run all tests**

Run: `cd orchestrator && ruff check src tests && python3 -m pytest -q`
Expected: all pass (existing mock tests unaffected, new pool test passes)

- [ ] **Step 5: Commit**

```bash
git add orchestrator/src/ingenieer/bridge_client.py orchestrator/tests/test_bridge_client.py
git commit -m "feat(L5): replace urllib.request with urllib3 PoolManager in HttpBridgeClient"
```

---

### Task 4: HTTP Edge-Case Test Harness

**Files:**
- Create: `orchestrator/tests/test_bridge_http.py`

This task creates a real HTTP server in tests to exercise edge cases that can't be tested with mocks.

- [ ] **Step 1: Write the test harness and all edge-case tests**

```python
# orchestrator/tests/test_bridge_http.py
"""Edge-case tests for HttpBridgeClient against a real HTTP server."""

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest

from ingenieer.bridge_client import HttpBridgeClient
from ingenieer.bridge_errors import BridgePermanentError, BridgeTimeoutError, BridgeTransientError


class _Handler(BaseHTTPRequestHandler):
    """Test HTTP handler with configurable behavior per path."""

    # Class-level response config — set before each test
    responses: dict = {}

    def do_GET(self):
        self._handle()

    def do_POST(self):
        # Read request body to avoid broken pipe
        length = int(self.headers.get("Content-Length", 0))
        self.rfile.read(length)
        self._handle()

    def _handle(self):
        cfg = self.responses.get(self.path, {"status": 404, "body": "{}"})

        if "delay" in cfg:
            time.sleep(cfg["delay"])

        self.send_response(cfg["status"])
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        body = cfg["body"]
        if isinstance(body, str):
            body = body.encode()
        self.wfile.write(body)

    def log_message(self, *args):
        pass  # suppress stderr


@pytest.fixture()
def bridge_server():
    """Start a local HTTP server, yield (url, handler_class), stop on teardown."""
    server = HTTPServer(("127.0.0.1", 0), _Handler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{port}", _Handler
    server.shutdown()


def test_successful_fingerprint(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/model-fingerprint": {
            "status": 200,
            "body": json.dumps({"modelFingerprint": "abc123"}),
        },
    }
    client = HttpBridgeClient(url, timeout_sec=5.0, deadline_sec=10.0, max_retries=0)
    assert client.get_model_fingerprint() == "abc123"


def test_503_then_200_retries_successfully(bridge_server):
    url, handler = bridge_server
    call_count = {"n": 0}
    original_handle = handler._handle

    def counting_handle(self_handler):
        call_count["n"] += 1
        if call_count["n"] == 1:
            # First call: 503
            self_handler.send_response(503)
            self_handler.send_header("Content-Type", "application/json")
            self_handler.end_headers()
            self_handler.wfile.write(b'{"error":"unavailable"}')
        else:
            # Second call: 200
            self_handler.send_response(200)
            self_handler.send_header("Content-Type", "application/json")
            self_handler.end_headers()
            self_handler.wfile.write(json.dumps({"modelFingerprint": "retry-ok"}).encode())

    handler._handle = counting_handle
    client = HttpBridgeClient(
        url, timeout_sec=5.0, deadline_sec=10.0, max_retries=1, retry_backoff_sec=0.05
    )
    assert client.get_model_fingerprint() == "retry-ok"


def test_400_permanent_not_retried(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/model-fingerprint": {
            "status": 400,
            "body": '{"error":"bad request"}',
        },
    }
    client = HttpBridgeClient(url, timeout_sec=5.0, deadline_sec=10.0, max_retries=2)
    with pytest.raises(BridgePermanentError) as exc_info:
        client.get_model_fingerprint()
    assert exc_info.value.status_code == 400


def test_malformed_json_response(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/model-fingerprint": {"status": 200, "body": b"not json at all"},
    }
    client = HttpBridgeClient(url, timeout_sec=5.0, deadline_sec=10.0, max_retries=0)
    with pytest.raises(Exception):  # json.JSONDecodeError
        client.get_model_fingerprint()


def test_missing_fingerprint_field(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/model-fingerprint": {"status": 200, "body": json.dumps({"other": "data"})},
    }
    client = HttpBridgeClient(url, timeout_sec=5.0, deadline_sec=10.0, max_retries=0)
    with pytest.raises(BridgePermanentError, match="missing modelFingerprint"):
        client.get_model_fingerprint()


def test_slow_response_within_deadline(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/model-fingerprint": {
            "status": 200,
            "body": json.dumps({"modelFingerprint": "slow-ok"}),
            "delay": 0.3,
        },
    }
    client = HttpBridgeClient(url, timeout_sec=5.0, deadline_sec=10.0, max_retries=0)
    assert client.get_model_fingerprint() == "slow-ok"


def test_execute_returns_bridge_result_on_error(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/execute": {"status": 500, "body": '{"error":"boom"}'},
    }
    from ingenieer.models import CadIntentEnvelope

    client = HttpBridgeClient(url, timeout_sec=5.0, deadline_sec=10.0, max_retries=0)
    intent = CadIntentEnvelope(intentId="x", command="NoOp", parameters={})
    result = client.execute_intent(intent)
    assert not result.success
    assert "500" in (result.error_traceback or "")
```

- [ ] **Step 2: Run tests**

Run: `cd orchestrator && python3 -m pytest tests/test_bridge_http.py -q`
Expected: 7 passed

- [ ] **Step 3: Run full suite to check for regressions**

Run: `cd orchestrator && ruff check src tests && python3 -m pytest -q`
Expected: all pass

- [ ] **Step 4: Commit**

```bash
git add orchestrator/tests/test_bridge_http.py
git commit -m "test(L5): HTTP edge-case harness — retry, permanent error, malformed JSON, slow response"
```

---

### Task 5: Deadline Enforcement Test

**Files:**
- Modify: `orchestrator/tests/test_bridge_http.py` (add deadline test)

- [ ] **Step 1: Write deadline enforcement test**

Add to `orchestrator/tests/test_bridge_http.py`:

```python
def test_deadline_exceeded_raises_timeout(bridge_server):
    url, handler = bridge_server
    handler.responses = {
        "/v1/model-fingerprint": {
            "status": 503,
            "body": '{"error":"unavailable"}',
        },
    }
    # Very short deadline with retries — should hit deadline before exhausting retries
    client = HttpBridgeClient(
        url, timeout_sec=5.0, deadline_sec=0.1, max_retries=10, retry_backoff_sec=0.05
    )
    with pytest.raises(BridgeTimeoutError, match="deadline exceeded"):
        client.get_model_fingerprint()
```

- [ ] **Step 2: Run the test**

Run: `cd orchestrator && python3 -m pytest tests/test_bridge_http.py::test_deadline_exceeded_raises_timeout -q`
Expected: PASS

- [ ] **Step 3: Run full suite**

Run: `cd orchestrator && ruff check src tests && python3 -m pytest -q`
Expected: all pass

- [ ] **Step 4: Commit**

```bash
git add orchestrator/tests/test_bridge_http.py
git commit -m "test(L5): deadline enforcement — timeout before retry exhaustion"
```

---

### Task 6: Final Verification and Branch Push

**Files:** None (verification only)

- [ ] **Step 1: Full lint**

Run: `cd orchestrator && ruff check src tests`
Expected: All checks passed

- [ ] **Step 2: Full Python test suite**

Run: `cd orchestrator && python3 -m pytest -q`
Expected: all pass (should be ~140+ tests)

- [ ] **Step 3: C# build**

Run: `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release`
Expected: 0 warnings, 0 errors

- [ ] **Step 4: C# tests**

Run: `dotnet test icad-addin/InGENeer.IcadAddin.slnx -c Release --no-build --verbosity quiet`
Expected: 6 passed

- [ ] **Step 5: Push branch**

```bash
git push -u origin goal-1/l5-transport-hardening
```
