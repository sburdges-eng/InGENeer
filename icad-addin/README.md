# InGENeer iCAD bridge (C# spike)

**Purpose:** Wire DTOs and MVP **intent routing** for [`docs/BRIDGE_TRANSPORT.md`](../docs/BRIDGE_TRANSPORT.md). This is **not** a shipping Carlson add-in—there are **no ITC/Carlson API calls** here yet (architecture rule 4: add only with official doc snippets).

## Projects

| Project | Role |
|---------|------|
| **InGENeer.IcadBridge** | `CadIntentEnvelope`, `BridgeExecutionResult`, `IntentRouter` (`NoOp`, `PingHost`, `GetModelFingerprint`), `ModelFingerprintStore`. |
| **InGENeer.Bridge.LoopbackHost** | Dev-only **HTTP** listener (`GET /v1/model-fingerprint`, `POST /v1/execute`) so the Python orchestrator can use `bridge.mode: http` without iCAD. |

## Build

```bash
cd icad-addin
dotnet build InGENeer.Bridge.LoopbackHost/InGENeer.Bridge.LoopbackHost.csproj
```

Targets **.NET 10** in this repo; retarget (`TargetFramework`) if your Carlson host requires **.NET Framework** (e.g. `net48`).

## Run loopback host (manual test with Python)

```bash
dotnet run --project InGENeer.Bridge.LoopbackHost
```

Optional: `INGENEER_BRIDGE_URL=http://127.0.0.1:9876/` to change bind URL (must end with `/`).

From another shell (venv + editable `ingenieer`):

```bash
cd orchestrator
printf '%s' '{"schemaVersion":"1.0.0","intentId":"t1","command":"NoOp","parameters":{}}' > /tmp/intent.json
printf '%s' '{"bridge":{"mode":"http","http_base_url":"http://127.0.0.1:8765","timeout_sec":10}}' > /tmp/orch.json
.venv/bin/ingenieer-run /tmp/intent.json --config /tmp/orch.json
```

## Real iCAD add-in (next)

- Load **InGENeer.IcadBridge** (or merge types) into the Carlson add-in project.
- **Marshal** `IntentRouter.Execute` (or host-specific dispatcher) to the **UI thread**.
- Wrap document mutations in the host’s **transaction** with **rollback** on failure.
- Replace `ModelFingerprintStore` with a value derived from **documented** host APIs.
