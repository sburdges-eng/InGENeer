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

### VerifySurface

| Field | Value |
|-------|-------|
| **Risk** | `low` |
| **Execution** | Host queries the named surface and returns metadata. Read-only — no document mutation. |

**Parameters:**

```json
{
  "surface_name": "Existing Ground"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `surface_name` | `string` | yes | Name of the surface to query in the CAD document. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "point_count": 1024,
  "triangle_count": 2000,
  "bounds": [[minX, minY, minZ], [maxX, maxY, maxZ]]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `point_count` | `integer` | Number of surface definition points. |
| `triangle_count` | `integer` | Number of TIN triangles. |
| `bounds` | `[[number, number, number], [number, number, number]]` | Axis-aligned bounding box: `[min, max]` corners. |

**Design note:** Completes the Import → Verify lifecycle. First command that returns meaningful query data in telemetry, exercising the bridge's return-data path. No confirmation token needed (read-only, `low` risk).

### CreateAlignment

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host creates a named horizontal alignment from the given vertices on the specified layer. In Carlson/IntelliCAD, this is typically represented as a named `Polyline` with stationing XData or a `.cl` centerline file. |

**Parameters:**

```json
{
  "name": "Main St CL",
  "points": [[1000.0, 2000.0, 100.0], [1200.0, 2100.0, 101.5], [1500.0, 2050.0, 99.8]],
  "start_station": 0.0,
  "layer": "ALIGNMENT",
  "type": "centerline"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `name` | `string` | yes | Unique alignment identifier within the document. |
| `points` | `array of [number, number, number]` | yes | 3D vertices defining horizontal geometry. Minimum 2 points. |
| `start_station` | `number` | yes | Starting station value (e.g. `0+00` = `0.0`). |
| `layer` | `string` | yes | Target CAD layer name. |
| `type` | `string` | no | Alignment classification: `"centerline"`, `"offset"`, `"curb"`. Defaults to `"centerline"` if omitted. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "length": 538.52,
  "station_range": [0.0, 538.52]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `length` | `number` | Calculated alignment length from vertices. |
| `station_range` | `[number, number]` | `[start_station, start_station + length]`. |

**Design note:** Alignments are the backbone of civil corridor design. This command creates the horizontal geometry; vertical profiles and cross-sections are future commands that reference the alignment by `name`. The host representation (Polyline + XData vs. Carlson `.cl` format) is a host decision — the intent envelope is representation-agnostic.

### CreateProfile

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host creates a vertical profile attached to a named alignment, defined by PVI (Point of Vertical Intersection) data. |

**Parameters:**

```json
{
  "alignment_name": "Main St CL",
  "profile_name": "Finished Grade",
  "pvi_data": [
    {"station": 0.0, "elevation": 100.0},
    {"station": 250.0, "elevation": 105.0, "curve_length": 100.0},
    {"station": 538.52, "elevation": 102.0}
  ],
  "layer": "C-ROAD-PROF"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `alignment_name` | `string` | yes | Name of the parent horizontal alignment. Must exist in the document. |
| `profile_name` | `string` | yes | Unique name for this vertical profile. |
| `pvi_data` | `array of PVI objects` | yes | Minimum 2 PVIs. Stations must be monotonically increasing. |
| `pvi_data[].station` | `number` | yes | Station along the parent alignment. |
| `pvi_data[].elevation` | `number` | yes | Elevation at this PVI. |
| `pvi_data[].curve_length` | `number` | no | Length of vertical curve centered on this PVI. |
| `pvi_data[].k_value` | `number` | no | K-value for vertical curve (rate of grade change). Alternative to `curve_length`. |
| `layer` | `string` | yes | Target CAD layer name. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "pvi_count": 3,
  "elevation_range": [100.0, 105.0]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `pvi_count` | `integer` | Number of PVIs in the profile. |
| `elevation_range` | `[number, number]` | `[min_elevation, max_elevation]` across all PVIs. |

**Design note:** Profiles reference alignments by `alignment_name`, establishing the horizontal-then-vertical civil design workflow. The PVI array uses structured objects (station + elevation) rather than flat coordinate tuples, exercising the bridge's nested parameter handling.

### CreateCrossSection

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host defines cross-section templates at specific stations along an alignment. |

**Parameters:**

```json
{
  "alignment_name": "Main St CL",
  "profile_name": "Finished Grade",
  "template_name": "Standard Road",
  "stations": [0.0, 50.0, 100.0, 250.0, 500.0],
  "layer": "C-ROAD-XSEC"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `alignment_name` | `string` | yes | Name of the parent horizontal alignment. |
| `profile_name` | `string` | yes | Name of the vertical profile to follow. |
| `template_name` | `string` | yes | Name of the cross-section template to apply. |
| `stations` | `array of numbers` | yes | List of stations where cross-sections should be generated. |
| `layer` | `string` | yes | Target CAD layer name. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "station_count": 5
}
```

| Field | Type | Notes |
|-------|------|-------|
| `station_count` | `integer` | Number of stations processed. |

**Design note:** Cross-sections bridge the gap between 2D alignment/profile and a 3D corridor model.

### CreateCorridorModel

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host combines alignment, profile, and cross-section data into a 3D corridor object. |

**Parameters:**

```json
{
  "name": "Phase 1 Road",
  "alignment_name": "Main St CL",
  "profile_name": "Finished Grade",
  "layer": "C-ROAD-CORR"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `name` | `string` | yes | Unique name for the corridor model. |
| `alignment_name` | `string` | yes | Name of the baseline horizontal alignment. |
| `profile_name` | `string` | yes | Name of the vertical profile baseline. |
| `layer` | `string` | yes | Target CAD layer for the corridor geometry. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "corridor_length": 538.52
}
```

| Field | Type | Notes |
|-------|------|-------|
| `corridor_length` | `number` | Total 3D length of the generated corridor. |

**Design note:** The final stage of the civil design pipeline. Corridors are high-complexity objects that typically result in significant document mutations.

### BalanceGrading

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host adjusts a proposed grading surface to balance cut and fill volumes against an existing ground surface. |

**Parameters:**

```json
{
  "existing_surface": "Existing Ground",
  "proposed_surface": "Proposed Grade",
  "tolerance": 10.0,
  "shrink_swell_factor": 1.15
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `existing_surface` | `string` | yes | Name of the base surface to compare against. |
| `proposed_surface` | `string` | yes | Name of the surface to modify for balance. |
| `tolerance` | `number` | yes | Acceptable net volume difference (cubic units). |
| `shrink_swell_factor` | `number` | no | Material expansion/contraction factor. Defaults to `1.0`. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "cut_volume": 1250.0,
  "fill_volume": 1245.0,
  "net_volume": 5.0,
  "balanced": true
}
```

| Field | Type | Notes |
|-------|------|-------|
| `cut_volume` | `number` | Total cut volume. |
| `fill_volume` | `number` | Total fill volume (adjusted by shrink/swell). |
| `net_volume` | `number` | Net volume (cut - fill). |
| `balanced` | `boolean` | True if `abs(net_volume) <= tolerance`. |

**Design note:** Surface balancing is an iterative optimization task. The host is responsible for the geometry adjustment; the orchestrator specifies the target state.

### CreateRetentionPond

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host generates grading geometry (breaklines/surface) for a retention pond based on a given outline. |

**Parameters:**

```json
{
  "outline_polyline_id": "pond-limit-01",
  "base_elevation": 95.0,
  "side_slope": 3.0,
  "berm_width": 10.0,
  "target_surface": "Existing Ground",
  "layer": "C-TOPO-POND"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `outline_polyline_id` | `string` | yes | Identifier (name/handle) of the polyline defining the pond top or bottom. |
| `base_elevation` | `number` | yes | Bottom elevation of the pond. |
| `side_slope` | `number` | yes | Ratio (H:V, e.g. `3.0` for 3:1). |
| `berm_width` | `number` | no | Width of the berm at the top of the pond. |
| `target_surface` | `string` | yes | Surface to tie the pond grading into. |
| `layer` | `string` | yes | Target CAD layer for pond geometry. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "pond_volume": 4500.0,
  "surface_area": 12000.0
}
```

| Field | Type | Notes |
|-------|------|-------|
| `pond_volume` | `number` | Total storage volume of the pond. |
| `surface_area` | `number` | Total footprint area of the pond grading. |

**Design note:** Pond design combines horizontal layout with vertical constraints.

### CreateSanitarySewerNetwork

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host creates a gravity-fed pipe network (pipes and manholes) referencing a horizontal alignment. |

**Parameters:**

```json
{
  "network_name": "Phase 1 Sewer",
  "alignment_name": "Main St CL",
  "structures": [
    {"station": 0.0, "type": "Manhole 48in", "rim_elevation": 100.0, "invert_elevation": 92.0},
    {"station": 250.0, "type": "Manhole 48in", "rim_elevation": 105.0, "invert_elevation": 91.5}
  ],
  "pipe_material": "PVC",
  "pipe_diameter": 8.0,
  "layer": "C-SSWR-NETW"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `network_name` | `string` | yes | Unique identifier for the network. |
| `alignment_name` | `string` | yes | Parent alignment for stationing. |
| `structures` | `array of structure objects` | yes | Nodes in the network. |
| `pipe_material` | `string` | yes | Material specification for pipes between structures. |
| `pipe_diameter` | `number` | yes | Internal pipe diameter (inches). |
| `layer` | `string` | yes | Target CAD layer name. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "structure_count": 2,
  "total_pipe_length": 250.0
}
```

| Field | Type | Notes |
|-------|------|-------|
| `structure_count` | `integer` | Number of manholes/structures created. |
| `total_pipe_length` | `number` | Total length of all pipes in the network. |

**Design note:** Utility modeling requires strict vertical (invert) and horizontal (station) alignment synchronization.

### AnalyzeStormDrainage

| Field | Value |
|-------|-------|
| **Risk** | `low` |
| **Execution** | Host performs hydraulic analysis on a pipe network and returns performance metrics. Read-only. |

**Parameters:**

```json
{
  "network_name": "Phase 1 Storm",
  "design_storm_years": 25,
  "runoff_coefficient": 0.85
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `network_name` | `string` | yes | Name of the network to analyze. |
| `design_storm_years` | `integer` | yes | Return period for the design storm. |
| `runoff_coefficient` | `number` | yes | Weighted C-factor for the drainage area. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "peak_discharge": 12.5,
  "max_velocity": 4.2,
  "capacity_exceeded": false
}
```

| Field | Type | Notes |
|-------|------|-------|
| `peak_discharge` | `number` | Calculated peak flow at the outlet (cfs). |
| `max_velocity` | `number` | Highest flow velocity in the network (fps). |
| `capacity_exceeded` | `boolean` | True if any pipe segment's capacity is exceeded by the peak flow. |

**Design note:** Read-only analysis command. No confirmation token required.

---

## Landscape Architecture commands

These intents represent landscape architecture primitives for planting, hardscape, and irrigation.

### PlacePlantingLayout

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host places plant symbols (blocks) at specified locations with metadata for growth and maintenance. |

**Parameters:**

```json
{
  "species_id": "QUERCUS_AGRIFOLIA",
  "points": [
    {"location": [1000.0, 2000.0, 0.0], "rotation": 45.0, "scale": 1.2},
    {"location": [1050.0, 2020.0, 0.0], "rotation": 90.0, "scale": 1.0}
  ],
  "mature_spread": 40.0,
  "layer": "L-PLNT-TREE",
  "container_size": "24 inch box"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `species_id` | `string` | yes | Botanical or common name ID for the plant. |
| `points` | `array of objects` | yes | Placement locations and geometry overrides. |
| `points[].location` | `[number, number, number]` | yes | 3D insertion point. |
| `points[].rotation` | `number` | no | Rotation in degrees. |
| `points[].scale` | `number` | no | Scale factor for the symbol. |
| `mature_spread` | `number` | yes | Diameter of mature canopy (feet/meters) for clash detection. |
| `layer` | `string` | yes | Target CAD layer name. |
| `container_size` | `string` | no | Specified planting size (e.g. "5 gallon"). |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "plant_count": 2,
  "canopy_coverage_area": 2513.27
}
```

| Field | Type | Notes |
|-------|------|-------|
| `plant_count` | `integer` | Total number of plants placed in the batch. |
| `canopy_coverage_area` | `number` | Total area covered by mature canopies (calculated by host). |

**Design note:** Batch placement minimizes transaction overhead. Canopy coverage is calculated by the host and returned for environmental/SIM analysis.

### CreatePavingArea

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host creates a parametric hardscape area (hatched polyline or slab) with material and drainage properties. |

**Parameters:**

```json
{
  "boundary_points": [[1000.0, 2000.0, 100.0], [1050.0, 2000.0, 100.0], [1050.0, 2050.0, 100.0], [1000.0, 2050.0, 100.0]],
  "material_type": "Permeable Pavers",
  "subbase_depth": 1.5,
  "permeability_coefficient": 0.85,
  "layer": "L-HRDS-PAVE"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `boundary_points` | `array of [number, number, number]` | yes | Closed boundary coordinates. |
| `material_type` | `string` | yes | Hardscape material specification. |
| `subbase_depth` | `number` | yes | Depth of sub-base material (feet/meters). |
| `permeability_coefficient` | `number` | yes | Runoff coefficient (0.0 to 1.0). |
| `layer` | `string` | yes | Target CAD layer name. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "paving_area": 2500.0,
  "perimeter_length": 200.0
}
```

| Field | Type | Notes |
|-------|------|-------|
| `paving_area` | `number` | Total calculated surface area. |
| `perimeter_length` | `number` | Total calculated boundary perimeter. |

**Design note:** Hardscape areas often drive drainage calculations. The `permeability_coefficient` is critical for SIM integration.

### DesignIrrigationZone

| Field | Value |
|-------|-------|
| **Risk** | `high` |
| **Execution** | Host generates an irrigation zone layout including heads, valves, and lateral piping. |

**Parameters:**

```json
{
  "zone_id": "HYDROZONE-A",
  "heads": [
    {"location": [1000.0, 2000.0, 0.0], "type": "Rotor", "radius": 35.0},
    {"location": [1035.0, 2000.0, 0.0], "type": "Rotor", "radius": 35.0}
  ],
  "pipe_material": "PVC Sch 40",
  "target_psi": 55.0,
  "layer": "L-IRRG-ZONE"
}
```

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `zone_id` | `string` | yes | Unique identifier for the irrigation hydrozone. |
| `heads` | `array of objects` | yes | Irrigation head locations and properties. |
| `pipe_material` | `string` | yes | Pipe material for lateral lines. |
| `target_psi` | `number` | yes | Design operating pressure at the head. |
| `layer` | `string` | yes | Target CAD layer name. |

**Return data** (in `telemetry` of `BridgeExecutionResult`):

```json
{
  "head_count": 2,
  "total_flow_gpm": 12.4,
  "pipe_length": 35.0
}
```

| Field | Type | Notes |
|-------|------|-------|
| `head_count` | `integer` | Total number of irrigation heads in the zone. |
| `total_flow_gpm` | `number` | Sum of flow rates for all heads in the zone. |
| `pipe_length` | `number` | Calculated total length of lateral piping. |

**Design note:** Irrigation zones are functional engineering units. The host calculates hydraulic requirements (GPM, pipe length) and returns them in telemetry.

---

## Proprietary CAD API–backed commands

**None in this repository yet.** Add rows here only after **official API** snippets exist (AutonomAtIon rule 4), and extend `ALLOWED_COMMANDS` / `IntentRouter` / this table together.

**Rule:** extend `parameters` per command with small JSON objects; document each in this file. Default new civil mutations to `high` until reviewed.

---

## Versioning

- Intent envelope `schemaVersion`: **1.1.0** (bump when breaking `CadIntentEnvelope` fields).  
- Wire contract `schemaVersion` for `build_contract_payload`: see `ingenieer.contracts.SCHEMA_VERSION` (separate from intent version).
