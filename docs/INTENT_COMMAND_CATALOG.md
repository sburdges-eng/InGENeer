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
| `sync_baseline` | Document fingerprint vs `modelFingerprintExpected` (stale guard) |
| `dispatch_execute` | Send intent to add-in; native `Transaction` + rollback on failure |
| `verify_result` | Independent check: live `GET /v1/model-fingerprint` must match `telemetry.modelFingerprintAfter` from dispatch; bounded retries **only** on transient transport failures |

---

## Commands (MVP placeholders)

Implementations are **stubs** until iCAD add-in commands exist. Names are stable for schema and tests.

| Command | Risk | Parameters (example) | Notes |
|---------|------|----------------------|-------|
| `NoOp` | low | `{}` | Connectivity / queue smoke test |
| `PingHost` | low | `{}` | Returns host id + build (from add-in) |
| `GetModelFingerprint` | low | `{}` | Returns hash / save counter for stale detection |
| `HighRiskStub` | high | `{}` | **Test-only** command to exercise human-confirmation validation |
| `CreatePointBlock` | high | `{"point": [x, y, z], "layer": "name", "label": "string", "attributes": {"key": "value"}}` | Create civil CAD point block (survey marker) |

---

## Future (civil / survey — align to Carlson workflows)

Add rows here only after **official API** snippets exist (AutonomAtIon rule 4). Examples of *candidate* names (not implemented):

- `DrawPolylineFromCoordinates` — WCS points, layer, color  
- `ImportLandXmlSurface` — relative path key under `paths` in outer contract, not inside envelope unless explicitly designed  

**Rule:** extend `parameters` per command with small JSON objects; document each in this file. Assign **risk** when adding commands (default new civil mutations to `high` until reviewed).

---

## Versioning

- Intent envelope `schemaVersion`: **1.1.0** (bump when breaking `CadIntentEnvelope` fields).  
- Wire contract `schemaVersion` for `build_contract_payload`: see `ingenieer.contracts.SCHEMA_VERSION` (separate from intent version).
