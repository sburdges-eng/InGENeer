# Design: DrawPolylineFromCoordinates and CreatePointBlocks Commands

**Date:** 2026-04-09
**Status:** Approved
**Scope:** L1 (intent contract) + L2 (orchestrator runtime) changes only. No L5/L6 (transport/host) work.

## Summary

Add two new civil CAD intent commands to the InGENeer orchestrator: `DrawPolylineFromCoordinates` for drawing coordinate-based polylines and `CreatePointBlocks` for batch-inserting survey point blocks. Both are `high` risk (require `humanConfirmationToken` in `execute` mode).

## Motivation

The current command catalog contains only infrastructure commands (`NoOp`, `PingHost`, `GetModelFingerprint`, `HighRiskStub`). These two commands are the first civil/survey domain intents, chosen because they represent the two most fundamental drawing primitives in civil CAD workflows: linework and point insertion.

## Commands

### DrawPolylineFromCoordinates

**Risk tier:** `high` (document mutation)

**Parameters:**

```json
{
  "points": [[x1, y1, z1], [x2, y2, z2], [x3, y3, z3]],
  "layer": "BOUNDARY",
  "closed": true,
  "color": "red"
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `points` | `array of [number, number, number]` | yes | 3D coordinates. Minimum 2 points. |
| `layer` | `string` | yes | Target CAD layer name. |
| `closed` | `boolean` | yes | Whether the polyline is mathematically closed. Affects area calculation, hatching, and linetype rendering in the CAD host. |
| `color` | `string` | no | Optional color override (host interprets). |

**Design notes:**
- `z` is always required. Civil/survey data is inherently 3D; omitting elevation leads to silent data loss.
- `closed` is explicit rather than inferred from first/last vertex matching. A closed polyline and an open polyline whose endpoints coincide are semantically different to CAD engines.
- The orchestrator validates structure only (array lengths, types). Coordinate validity (CRS, bounds) is the host's responsibility.

### CreatePointBlocks

**Risk tier:** `high` (document mutation)

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
    },
    {
      "location": [1050.0, 2010.0, 346.12],
      "number": 102,
      "elevation": 346.12,
      "description": "MAG NAIL"
    }
  ]
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `layer` | `string` | yes | Target CAD layer for all points in this batch. |
| `blockName` | `string` | yes | Block definition name to insert at each location. |
| `points` | `array of point objects` | yes | Minimum 1 point. |
| `points[].location` | `[number, number, number]` | yes | 3D insertion point. |
| `points[].number` | `integer` | no | Survey point number. |
| `points[].elevation` | `number` | no | Explicit elevation (may differ from `location[2]` for display purposes). |
| `points[].description` | `string` | no | Point description code (e.g. "IRON PIN", "MAG NAIL"). |
| `points[].attributes` | `object` | no | Arbitrary key-value block attributes passed to the host. |

**Design notes:**
- Batch by design. Survey point import is almost always a bulk operation (CSV of hundreds of points). One intent = one transaction = one fingerprint verification. This avoids N round-trips and N document hashes.
- `blockName` is at the top level (shared across the batch). If a workflow needs mixed block types, send multiple intents.
- `elevation` is separate from `location[2]` because survey workflows sometimes display a label elevation that differs from the geometric Z (e.g., geoid-corrected vs. ellipsoidal).

## Risk and Confirmation

Both commands follow the existing `high` risk pattern established by `HighRiskStub`:
- `dry_run` / `preview`: no confirmation token required.
- `execute`: requires non-empty `humanConfirmationToken`.

No changes to the confirmation logic in `collect_intent_validation_errors` are needed; the existing risk-tier check handles this.

## Files Changed

| File | Change |
|------|--------|
| `orchestrator/src/ingenieer/intent_validation.py` | Add both commands to `ALLOWED_COMMANDS` and `COMMAND_RISK` |
| `docs/INTENT_COMMAND_CATALOG.md` | Add command rows with parameter schemas and notes |
| `orchestrator/tests/test_intent_validation.py` | Validation tests: allowlist acceptance, high-risk confirmation enforcement |
| `orchestrator/tests/test_orchestrator.py` | Pipeline round-trip tests for both commands (mock bridge) |

## Files NOT Changed

- `models.py` — `parameters` is already `dict[str, Any]`; command-specific Pydantic models are a future concern per the existing docstring.
- `cad_intent_envelope.schema.json` — `parameters` is already `"type": "object"` with no constraints. No schema version bump needed.
- `contracts.py`, `wire.py`, `audit.py` — no changes to wire format or audit events.

## Out of Scope

- Parameter-level validation in the orchestrator (e.g., minimum point count, coordinate bounds). These are host responsibilities per architecture rule 1 (orchestrator does not calculate geometry).
- C# iCAD add-in implementation (L6). The `dispatch_execute` phase remains a stub/mock.
- Per-command Pydantic parameter models. The `CadIntentEnvelope.parameters` field stays as `dict[str, Any]` for now.
