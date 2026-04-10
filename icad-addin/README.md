# InGENeer iCAD bridge (C# spike)

**Purpose:** Wire DTOs and MVP **intent routing** for [`docs/BRIDGE_TRANSPORT.md`](../docs/BRIDGE_TRANSPORT.md). This is **not** a shipping Carlson add-in—there are **no ITC/Carlson API calls** here yet (architecture rule 4: add only with official doc snippets).

## Projects

| Project | Role |
|---------|------|
| **InGENeer.IcadBridge** | `CadIntentEnvelope`, `BridgeExecutionResult`, `IntentRouter`, `ModelFingerprintStore` (revision → **64-char SHA-256 hex**), **`BridgeHttpTransport`** (`GET /v1/model-fingerprint`, `POST /v1/execute`). |
| **InGENeer.Bridge.LoopbackHost** | Dev-only **HTTP** listener; each request is handled by **`BridgeHttpTransport`** so Python can use `bridge.mode: http` without iCAD. |

## `BridgeHttpTransport` and loopback fingerprints

- **`GET /v1/model-fingerprint`** returns JSON `{ "modelFingerprint": "<64-char lowercase hex>" }` from `ModelFingerprintStore.Snapshot()`.
- **`POST /v1/execute`** parses a `CadIntentEnvelope`, runs `IntentRouter.Execute`, returns `BridgeExecutionResult` (including `invariants` and telemetry such as `modelFingerprintAfter` on success).

Loopback fingerprints are **opaque SHA-256 hex strings (64 characters)**, derived from a revision counter and salt inside `ModelFingerprintStore`. They are **not** the old placeholder string `loopback-stub-fingerprint`. After each successful **`execute`** dispatch that commits, the store advances revision and the fingerprint changes.

**Stale guard (`modelFingerprintExpected`):** If you put `modelFingerprintExpected` on the intent envelope, `sync_baseline` compares it to the live `GET` value. For manual runs against this C# host, **call `GET /v1/model-fingerprint` first** and set `modelFingerprintExpected` to that string (or omit the field to skip the guard). The sample `intent.json` below leaves it unset.

## Build

From the **repository root** (matches CI):

```bash
dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release
```

Minimal loopback-only build:

```bash
cd icad-addin
dotnet build InGENeer.Bridge.LoopbackHost/InGENeer.Bridge.LoopbackHost.csproj
```

Targets **.NET 10** in this repo; retarget (`TargetFramework`) if your Carlson host requires **.NET Framework** (e.g. `net48`).

## Verification

- **C#:** `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release` (from repo root).
- **Python orchestrator:** after `cd orchestrator && python3 -m venv .venv && .venv/bin/pip install -e ".[dev]"`, run `.venv/bin/pytest -q` — **23** tests should pass.

## Run loopback host (manual test with Python)

```bash
cd icad-addin
dotnet run --project InGENeer.Bridge.LoopbackHost
```

Optional: `INGENEER_BRIDGE_URL` overrides the bind URL (default `http://127.0.0.1:8765/`; must end with `/`). Set `bridge.http_base_url` in orchestrator config to the same origin.

From another shell (venv + editable `ingenieer`):

```bash
cd orchestrator
printf '%s' '{"schemaVersion":"1.1.0","intentId":"t1","command":"NoOp","parameters":{},"executionMode":"execute"}' > /tmp/intent.json
printf '%s' '{"bridge":{"mode":"http","http_base_url":"http://127.0.0.1:8765","timeout_sec":10}}' > /tmp/orch.json
.venv/bin/ingenieer-run /tmp/intent.json --config /tmp/orch.json
```

## Real iCAD add-in (next)

- Load **InGENeer.IcadBridge** (or merge types) into the Carlson add-in project.
- **Marshal** `IntentRouter.Execute` (or host-specific dispatcher) to the **UI thread**.
- Wrap document mutations in the host’s **transaction** with **rollback** on failure.
- Replace `ModelFingerprintStore` with a value derived from **documented** host APIs.
