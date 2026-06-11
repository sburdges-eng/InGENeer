# CAD bridge transport (orchestrator ↔ iCAD add-in)

**Status:** Default integration is **HTTP on localhost** with JSON bodies. The Python package implements **`mock`** (in-process, no I/O) and **`http`** clients; the reference host listener is the **C# spike** under [`icad-addin/`](../icad-addin/).

**Canonical types:** [`CadIntentEnvelope`](../orchestrator/src/ingenieer/models.py) (request), [`BridgeExecutionResult`](../orchestrator/src/ingenieer/wire.py) (execution response). Intent JSON Schema: [`schemas/cad_intent_envelope.schema.json`](../schemas/cad_intent_envelope.schema.json).

---

## Why HTTP (for now)

- Easy to drive from Python with the standard library (`urllib`) and to test with a threaded `HTTPServer`.
- Clear boundary for **air-gapped** Cursor workspaces: the contract is **URLs + JSON**, not shared memory.
- The add-in can host a **localhost-only** listener (loopback bind) on a fixed port; adjust for your Carlson/ITC deployment rules if they restrict listeners.

**Alternatives** (not implemented here): named pipes, stdio JSON lines, gRPC. If you change transport, version the path prefix (e.g. `/v1/`) and update this doc.

---

## Base URL

- Config: `OrchestratorConfig.bridge.http_base_url` (default `http://127.0.0.1:8765`).
- All paths below are relative to that base (no trailing slash required; client normalizes).

---

## Timeouts and HTTP retries

- Config: `OrchestratorConfig.bridge.timeout_sec` (default **30** seconds, range 1–120).
- Applies per HTTP request (fingerprint GET and execute POST separately).
- Config: `OrchestratorConfig.bridge.deadline_sec` (default **60** seconds, range 1–600).
- Applies to the full wall-clock duration of one bridge call, including retries and backoff; once exceeded, the call fails as a transient timeout instead of stretching until retry exhaustion.
- **Transient retries** (orchestrator only, not inside CAD transactions): `bridge.http_max_retries` (default **2** extra attempts) and `bridge.http_retry_backoff_sec` (default **0.25** s, exponential backoff with jitter). Retries apply to connection errors and HTTP **429**, **502**, **503**, **504** only—not to logical 4xx failures.
- For HTTP **429** and **503**, a valid `Retry-After` response header overrides the normal exponential backoff. Integer-second and HTTP-date formats are supported. Invalid `Retry-After` values are ignored and the client falls back to the default bounded backoff.
- Timeout handling is explicit at the transport layer: connect/read timeouts are surfaced as **transient transport failures**, so `verify_result` may retry them but schema/protocol mismatches still fail closed.
- The Python HTTP client keeps a **single reusable keep-alive connection** per `HttpBridgeClient` instance. If the host closes the socket or a transient transport fault occurs, the client drops that connection and reconnects on the next attempt.
- Each `HttpBridgeClient` instance also maintains a **simple circuit breaker**:
  - `bridge.circuit_breaker_threshold` (default **5**) counts consecutive transient transport failures before the breaker opens.
  - `bridge.circuit_breaker_cooldown_sec` (default **30.0** seconds) controls how long the breaker remains open before a half-open probe is allowed.
  - State machine: `closed` → `open` after N consecutive transient failures; `open` → `half_open` after cooldown; `half_open` → `closed` on success; `half_open` → `open` on transient failure.
  - While `open`, requests fail immediately with `circuit breaker open: bridge unavailable` and no network call is made.
  - Permanent failures (`4xx`, `500`, invalid JSON) do **not** trip the breaker.
- Each HTTP call also records structured transport telemetry with:
  - `request_id`
  - `attempts`
  - `total_duration_sec`
  - `retried_status_codes`
  - `retry_after_used`
  - `final_status_code`
  - `error_class`
  - `circuit_breaker_state`

---

## Post-dispatch verification (orchestrator)

After a successful `POST /v1/execute`, the orchestrator **re-reads** `GET /v1/model-fingerprint` and requires equality with **`telemetry.modelFingerprintAfter`** from the execution response.

- Hosts **must** populate `modelFingerprintAfter` for every successful execute (including `dry_run` / `preview`, where it should match the pre-mutation fingerprint).
- On mismatch, verification fails **without** retry (deterministic failure).
- **Verification retries** (`OrchestratorConfig.max_verification_attempts`, default **3**, with `verification_backoff_sec`) apply only when the fingerprint GET fails with a **transient** transport error (as reported by the client).
- For baseline sync and dispatch, the orchestrator includes a `transport` object in `PhaseResult.data` so retry behavior is visible in pipeline results and downstream audit records.
- The `transport` object is a dict view of `TransportTelemetry`, including retry history, final status, error class, and the breaker state observed when the call completed or failed.

---

## Idempotency and correlation

- Every request body includes `intentId`. The host **should** treat duplicate `intentId` + same `command` + same payload as idempotent (no double mutation) when the first attempt already committed—exact policy is host-specific.
- Return the same `intentId` in `telemetry` on the execution result for log correlation.
- The orchestrator adds `X-Request-Id` on every HTTP call. Formats are:
  - baseline GET: `model-fingerprint:attempt-N`
  - verify GET: `verify-fingerprint:{intentId}:attempt-N`
  - execute POST: `{intentId}:attempt-N`
- The orchestrator adds `X-Idempotency-Key: {intentId}` on `POST /v1/execute` only, so retried dispatches can be deduplicated by the bridge.

---

## `GET /v1/model-fingerprint`

**Purpose:** `sync_baseline` compares this value to `modelFingerprintExpected` on the envelope (when set). `verify_result` uses it as an independent post-dispatch read.

**Response:** `200` and JSON:

```json
{
  "modelFingerprint": "opaque-string-from-host"
}
```

**Errors:** Non-2xx or invalid JSON → orchestrator treats baseline sync as failed (host unreachable or protocol mismatch).

---

## `POST /v1/execute`

**Purpose:** Run one validated intent on the **UI thread** inside the host’s **native transaction** (see architecture rules).

**Request:** `Content-Type: application/json` — JSON object matching `CadIntentEnvelope` (same keys as Pydantic `model_dump()`, e.g. `schemaVersion`, `intentId`, `command`, `parameters`, `executionMode`, optional `humanConfirmationToken` / `humanConfirmationId`, `targetDocumentRef`, `modelFingerprintExpected`).

**Raw JSON vs Python defaults:** If a payload is validated **only** against the intent JSON Schema (no Pydantic), it **must** include `schemaVersion` (`1.1.0`) and `executionMode` — both are required in [`schemas/cad_intent_envelope.schema.json`](../schemas/cad_intent_envelope.schema.json). The Python orchestrator CLI parses with `CadIntentEnvelope.model_validate` first, which defaults `schemaVersion` to `1.1.0` and `executionMode` to `execute` before JSON Schema runs in `validate_intent`.

**Execution modes:** When `executionMode` is `dry_run` or `preview`, the host must **not** commit document mutations; still return success telemetry including `modelFingerprintAfter` unchanged. When `execute`, the host may mutate and must set `modelFingerprintAfter` to the post-commit document fingerprint.

**Response:** `200` and JSON object matching `BridgeExecutionResult`:

| Field | Type | Notes |
|-------|------|-------|
| `schemaVersion` | string | Wire contract; align with `ingenieer.contracts.SCHEMA_VERSION` when wrapping outer contracts |
| `success` | boolean | Required |
| `stdout` | string | Optional human/log text |
| `error_traceback` | string or null | Populated on failure |
| `telemetry` | object | **Include** `intentId`, `command`, `executionMode`, and **`modelFingerprintAfter`** (string) on success |
| `invariants` | array of string | Optional; defaults exist on Python model |

**Errors:** Non-2xx → dispatch phase fails with message including status. Body, if present, may still be parsed for `error_traceback`.

---

## Human confirmation (high-risk)

For commands classified **high** in the intent catalog / `COMMAND_RISK`, `executionMode: execute` requires `humanConfirmationToken` at orchestrator validation. The loopback host should **also** reject `HighRiskStub` + `execute` without a token if hit directly—defense in depth.

Threat model: loopback assumes a **trusted local machine**; tokens are not a substitute for auth on shared or remote bridges.

---

## Mock mode (tests / CI)

- `OrchestratorConfig.bridge.mode = "mock"` uses an in-process client: no sockets.
- Fingerprint: `bridge.mock_model_fingerprint` (default `stub-fingerprint`).
- Execute: deterministic success `BridgeExecutionResult` unless `parameters._bridge_execute_fail` is **true** (tests only).

---

## Security notes

- Bind to **loopback** only unless you have explicit network isolation.
- Do not pass **absolute** paths or `..` segments through contract `paths` buckets (orchestrator [`contracts.py`](../orchestrator/src/ingenieer/contracts.py) already guards its own contract payloads).
- This transport is **not** authentication-grade; it assumes a trusted local machine.

---

## Schema handoff for CAD workspaces

Run [`tools/scripts/copy_schema_handoff.sh`](../tools/scripts/copy_schema_handoff.sh) to emit `schemas/cad_intent_envelope.schema.json` and sample envelopes into `./handoff/` (gitignored) for paste-in to an air-gapped Cursor window.

---

## Document control

- **When to update:** Endpoint or field changes; new transport implementation; port or versioning policy changes.
