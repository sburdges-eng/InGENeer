# auracad — COGO Semantics Extraction

**Status:** Extracted 2026-06-11 (Phase 2.5.2)  
**Stage 3 disposition:** **Promote** → `libs/survey_core` / `libs/geometry_core`

## Source files

| Area | Header | Implementation |
|------|--------|----------------|
| Point type | `core/include/aura/core/cogo_point.h` | JSON via nlohmann |
| Bearings / DMS | `geom/include/aura/geom/cogo_math.h` | `geom/src/cogo_math.cpp` |
| Survey ops | `geom/include/aura/geom/cogo_compute.h` | `geom/src/cogo_compute.cpp` |

## Conventions

- **Azimuth:** North-based, clockwise; 0 = North
- **Inverse:** `hypot(dN, dE)`, azimuth `atan2(dE, dN)` normalized `[0, 2π)`
- **Intersections:** bearing–bearing, distance–distance (two solutions), bearing–distance (two solutions); degenerate → empty
- **Curves:** radius + delta → tangent, arc, chord, external, mid-ordinate; PC/PT from PI
- **Station/offset:** along baseline + perpendicular (right positive)
- **Bearings:** Quadrant NE/SE/SW/NW + DMS parse/format

## Port notes

- Align hardcoded `1e-12` tolerances in `cogo_compute.cpp` with [numeric-policy.md](numeric-policy.md) constants
- COGO is **open Core** (Apache-2.0) — auracad header already SPDX Apache-2.0 on numeric policy; verify all COGO files before copy

## Tests to mine at Stage 3

Search `auracad/tests/` for `cogo` harness tests when promoting code.
