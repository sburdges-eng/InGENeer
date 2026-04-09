# InGENeer intent command catalog

Commands are **declarative intent names** only. The C# **CAD execution layer** (Carlson iCAD / IntelliCAD) maps them to documented APIs—never the reverse.

**Envelope:** see `schemas/cad_intent_envelope.schema.json` and Pydantic `CadIntentEnvelope` in `orchestrator/src/ingenieer/models.py`.

---

## Execution modes (`executionMode`)

| Mode | Orchestrator / host behavior |
|------|------------------------------|
| `dry_run` | Host must **not** commit document mutations; returns planned effect summary where applicable. |
| `preview` | Same non-commit contract as `dry_run`; hosts may attach richer “would change” detail later. |
| `execute` | Host may run transactional mutations and **must** publish `modelFingerprintAfter` in execution telemetry for post-dispatch verification. |

CLI: `ingenieer-run --dry-run` / `--preview` override the envelope before validation.

---

## Risk tiers (`COMMAND_RISK` in `orchestrator/src/ingenieer/intent_validation.py`)

| Tier | `execute` mode rule |
|------|---------------------|
| `low` | No human confirmation token required. |
| `high` | Requires non-empty `humanConfirmationToken` (and host should re-check for direct HTTP callers). |

Use `dry_run` / `preview` to plan without an approval token.

---

## Lifecycle

| Phase (orchestrator) | Responsibility |
|----------------------|----------------|
| `validate_intent` | Pydantic / JSON Schema on envelope; allowlist; high-risk confirmation rule |
| `sync_baseline` | `GET /v1/model-fingerprint` vs `modelFingerprintExpected` (stale guard) |
| `dispatch_execute` | `POST /v1/execute` — native `Transaction` + rollback on failure in real host |
| `verify_result` | Independent check: live `GET /v1/model-fingerprint` must match `telemetry.modelFingerprintAfter` from dispatch; bounded retries **only** on transient transport failures |

---

## Commands implemented by the bridge HTTP API

These intents are accepted by **`POST /v1/execute`** on the loopback host (`InGENeer.Bridge.LoopbackHost`) and routed in `icad-addin/InGENeer.IcadBridge/IntentRouter.cs`. They exercise transport, fingerprinting, and risk rules; they **do not** call proprietary Carlson/ITC drawing APIs.

| Command | Risk | Parameters (example) | Notes |
|---------|------|----------------------|-------|
| `NoOp` | low | `{}` | Connectivity / queue smoke test |
| `PingHost` | low | `{}` | Returns host id + build (from add-in assembly) |
| `GetModelFingerprint` | low | `{}` | Echoes current `modelFingerprint` from the host store |
| `HighRiskStub` | high | `{}` | **Test-only** command to exercise human-confirmation validation |

The loopback host publishes **opaque SHA-256 hex** fingerprints from `ModelFingerprintStore` (revision-based). A real add-in should replace the store internals with values from **documented** host APIs while keeping the same HTTP contract.

---

## Proprietary CAD API–backed commands

**None in this repository yet.** Add rows here only after **official API** snippets exist (AutonomAtIon rule 4), and extend `ALLOWED_COMMANDS` / `IntentRouter` / this table together.

**Rule:** extend `parameters` per command with small JSON objects; document each in this file. Default new civil mutations to `high` until reviewed.

---

## Versioning

- Intent envelope `schemaVersion`: **1.1.0** (bump when breaking `CadIntentEnvelope` fields).  
- Wire contract `schemaVersion` for `build_contract_payload`: see `ingenieer.contracts.SCHEMA_VERSION` (separate from intent version).
