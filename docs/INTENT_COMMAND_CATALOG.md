# InGENeer intent command catalog

Commands are **declarative intent names** only. The C# **CAD execution layer** (Carlson iCAD / IntelliCAD) maps them to documented APIs—never the reverse.

**Envelope:** see `schemas/cad_intent_envelope.schema.json` and Pydantic `CadIntentEnvelope` in `orchestrator/src/ingenieer/models.py`.

---

## Lifecycle

| Phase (orchestrator) | Responsibility |
|----------------------|----------------|
| `validate_intent` | Pydantic / JSON Schema on envelope |
| `sync_baseline` | Document fingerprint vs `modelFingerprintExpected` (stale guard) |
| `dispatch_execute` | Send intent to add-in; native `Transaction` + rollback on failure |
| `verify_result` | Programmatic checks from CAD API (clash, constraints)—bounded retries |

---

## Commands (MVP placeholders)

Implementations are **stubs** until iCAD add-in commands exist. Names are stable for schema and tests.

| Command | Parameters (example) | Notes |
|---------|----------------------|--------|
| `NoOp` | `{}` | Connectivity / queue smoke test |
| `PingHost` | `{}` | Returns host id + build (from add-in) |
| `GetModelFingerprint` | `{}` | Returns hash / save counter for stale detection |

---

## Future (civil / survey — align to Carlson workflows)

Add rows here only after **official API** snippets exist (AutonomAtIon rule 4). Examples of *candidate* names (not implemented):

- `CreatePointBlock` — points, layers, attributes  
- `DrawPolylineFromCoordinates` — WCS points, layer, color  
- `ImportLandXmlSurface` — relative path key under `paths` in outer contract, not inside envelope unless explicitly designed  

**Rule:** extend `parameters` per command with small JSON objects; document each in this file.

---

## Versioning

- Intent envelope `schemaVersion`: **1.0.0** (bump when breaking `CadIntentEnvelope` fields).  
- Wire contract `schemaVersion` for `build_contract_payload`: see `ingenieer.contracts.SCHEMA_VERSION`.
