# ADR-0013 — Survey Core Engine Decomposition

**Status:** Accepted (D14).

## Decision
The Survey Engine decomposes into focused engines, each a separately testable C++23 module:

```
Survey Engine
├─ Parcel Engine        parcel fabric, lot/block, area closure
├─ Boundary Engine      deed/evidence weighting interfaces, boundary resolution (advisory AI domain)
├─ Surface Engine       TIN lifecycle (ADR-0012), surfaces, volumes, contours
├─ Coordinate Engine    CRS, state plane, geodetics, adjustments (PROJ-backed)
├─ Point Cloud Engine   ingestion, indexing, classification interfaces (PDAL-backed)
├─ Alignment Engine     horizontal/vertical alignments, stationing
├─ Corridor Engine      corridor modeling from alignments + surfaces
└─ QA Engine            closure validation, surface validation, standards lint, deliverable verification
```

Monorepo mapping: `libs/{survey_core, parcel_core, surface_core, pointcloud_core, coordinate_core, geometry_core, interop_core, ai_core, audit_core}` — boundary/alignment/corridor/QA live in `survey_core`; parcel, surface, point cloud, coordinate have dedicated libs per the approved layout.

## Consequences
Engine boundaries define test boundaries and the automation API surface; the QA Engine is the deterministic half of the QA/QC agent (the AI half lives in closed Aura Intelligence and only proposes).
