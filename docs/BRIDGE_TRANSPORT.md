# CAD bridge transport (orchestrator ↔ iCAD add-in)

**Status:** Default integration is **HTTP on localhost** with JSON bodies. The Python package implements **`mock`** (in-process, no I/O) and **`http`** clients; a dev reference listener is **`dotnet run --project icad-addin/InGENeer.Bridge.LoopbackHost`** (see [`icad-addin/README.md`](../icad-addin/README.md)).

**Canonical types:** [`CadIntentEnvelope`](../orchestrator/src/ingenieer/models.py) (request), [`BridgeExecutionResult`](../orchestrator/src/ingenieer/wire.py) (execution response). Intent JSON Schema: [`schemas/cad_intent_envelope.schema.json`](../schemas/cad_intent_envelope.schema.json).

---

## Why HTTP (for now)

- Easy to drive from Python with the standard library (`urllib`) and to test with a threaded `HTTPServer`.
- Clear boundary for **air-gapped** Cursor workspaces: the contract is **URLs + JSON**, not shared memory.
- The add-in can host a small **localhost-only** listener (loopback bind) on a fixed port; adjust for your Carlson/ITC deployment rules if they restrict listeners.

**Alternatives** (not implemented here): named pipes, stdio JSON lines, gRPC. If you change transport, version the path prefix (e.g. `/v1/`) and update this doc.

---

## Base URL

- Config: `OrchestratorConfig.bridge.http_base_url` (default `http://127.0.0.1:8765`).
- All paths below are relative to that base (no trailing slash required; client normalizes).

---

## Timeouts

- Config: `OrchestratorConfig.bridge.timeout_sec` (default **30** seconds, range 1–120).
- Applies per HTTP request (fingerprint GET and execute POST separately).

---

## Idempotency and correlation

- Every request body includes `intentId`. The host **should** treat duplicate `intentId` + same `command` + same payload as idempotent (no double mutation) when the first attempt already committed—exact policy is host-specific.
- Return the same `intentId` in `telemetry` on the execution result for log correlation.

---

## `GET /v1/model-fingerprint`

**Purpose:** `sync_baseline` compares this value to `modelFingerprintExpected` on the envelope (when set).

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

**Request:** `Content-Type: application/json` — JSON object matching `CadIntentEnvelope` (same keys as Pydantic `model_dump()`, e.g. `schemaVersion`, `intentId`, `command`, `parameters`, optional `targetDocumentRef`, `modelFingerprintExpected`).

**Response:** `200` and JSON object matching `BridgeExecutionResult`:

| Field | Type | Notes |
|-------|------|--------|
| `schemaVersion` | string | Wire contract; align with `ingenieer.contracts.SCHEMA_VERSION` when wrapping outer contracts |
| `success` | boolean | Required |
| `stdout` | string | Optional human/log text |
| `error_traceback` | string or null | Populated on failure |
| `telemetry` | object | Scalar values recommended; include `intentId`, `command` |
| `invariants` | array of string | Optional; defaults exist on Python model |

**Errors:** Non-2xx → dispatch phase fails with message including status. Body, if present, may still be parsed for `error_traceback`.

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

## Document control

- **When to update:** Endpoint or field changes; new transport implementation; port or versioning policy changes.
