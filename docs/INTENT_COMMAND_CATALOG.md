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

## Civil drawing commands

These intents represent civil/survey drawing primitives. The orchestrator validates envelope structure and risk rules; **parameter-level validation (coordinate bounds, layer existence, block definition lookup) is the host's responsibility** per architecture rule 1.

### DrawPolylineFromCoordinates

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host draws a polyline through the given 3D coordinates on the specified layer. |

**Parameters:**

```json
{
  "points": [[1000.0, 2000.0, 100.0], [1050.0, 2010.0, 101.5], [1100.0, 2005.0, 99.8]],
  "layer": "BOUNDARY",
  "closed": true,
  "color": "red"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `points` | `array of [number, number, number]` | yes | 3D coordinates (x, y, z). Minimum 2 points. |
| `layer` | `string` | yes | Target CAD layer name. |
| `closed` | `boolean` | yes | Mathematically closed polyline. Affects area, hatching, and linetype rendering. Not the same as first/last vertex coincidence. |
| `color` | `string` | no | Optional color override (host interprets). |

### CreatePointBlocks

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host inserts block references at each point location within a single transaction. |

**Parameters:**

```json
{
  "layer": "SURVEY",
  "blockName": "IRON_PIN",
  "points": [
    {
      "location": [1000.0, 2000.0, 345.67],
      "number": 101,
      "elevation": 345.67,
      "description": "IRON PIN",
      "attributes": {"crew": "A", "date": "2026-04-09"}
    }
  ]
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `layer` | `string` | yes | Target CAD layer for all points in this batch. |
| `blockName` | `string` | yes | Block definition name to insert at each location. |
| `points` | `array of point objects` | yes | Minimum 1 point. |
| `points[].location` | `[number, number, number]` | yes | 3D insertion point. |
| `points[].number` | `integer` | no | Survey point number. |
| `points[].elevation` | `number` | no | Display elevation (may differ from `location[2]` for geoid vs. ellipsoidal). |
| `points[].description` | `string` | no | Point description code (e.g. "IRON PIN", "MAG NAIL"). |
| `points[].attributes` | `object` | no | Arbitrary key-value block attributes passed to the host. |

**Design note:** Batch by design. Survey point import is a bulk operation; one intent = one transaction = one fingerprint verification, avoiding N round-trips.

### ImportLandXmlSurface

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host imports a LandXML surface file into the document on the specified layer. |

**Parameters:**

```json
{
  "landxml_path_key": "surface_file",
  "surface_name": "Existing Ground",
  "layer": "TOPO_SURFACE"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `landxml_path_key` | `string` | yes | Key into the outer contract's `paths` dictionary. The orchestrator resolves the actual file path; the intent envelope never contains absolute paths (air-gapped security model). |
| `surface_name` | `string` | yes | Name for the imported surface within the CAD document. |
| `layer` | `string` | yes | Target CAD layer for the surface. |

**Design note:** External files are referenced by key, not by path. The `landxml_path_key` maps to an entry in the `paths` bucket of the outer contract payload (`contracts.py`), maintaining the air-gapped security model where the intent envelope never carries absolute filesystem paths.

---

## Proprietary CAD API–backed commands

**None in this repository yet.** Add rows here only after **official API** snippets exist (AutonomAtIon rule 4), and extend `ALLOWED_COMMANDS` / `IntentRouter` / this table together.

**Rule:** extend `parameters` per command with small JSON objects; document each in this file. Default new civil mutations to `high` until reviewed.

---

## Versioning

- Intent envelope `schemaVersion`: **1.1.0** (bump when breaking `CadIntentEnvelope` fields).  
- Wire contract `schemaVersion` for `build_contract_payload`: see `ingenieer.contracts.SCHEMA_VERSION` (separate from intent version).
