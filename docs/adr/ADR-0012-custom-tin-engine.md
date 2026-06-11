# ADR-0012 — Custom In-House TIN Engine

**Status:** Accepted (D13).

## Context
TIN is not magic; survey firms have built TINs for decades. CGAL (GPL) is excluded (ADR-0011), and a focused engine is achievable.

## Decision
Build the TIN engine in-house within `surface_core`. Core requirements: Delaunay (constrained), breaklines, edge swaps, volume calculations, contour generation, surface queries. Foundation: ported Shewchuk-style filtered/adaptive predicates from auracad (orient2d complete; orient3d/incircle in progress at time of port).

## Rejected
- CGAL triangulations (GPL); Triangle library (non-commercial license); GEOS-only (no constrained Delaunay fit for TIN-with-breaklines at scale).

## Consequences
Owned IP inside the open Core; robustness on degenerate field data is the hard 20% (risk R-9, assumption A-5); must validate against the TOTaLi surface pipeline as reference oracle (ADR-0002) and scale to 100M+ points via out-of-core design (R-7.3).
