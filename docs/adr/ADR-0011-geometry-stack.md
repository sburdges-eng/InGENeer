# ADR-0011 — Geometry Stack: Survey-Native Core, Permissive Backend, OCCT Satellite

**Status:** Accepted (D12, amended in Round 2/3 to drop CGAL).

## Context
Survey CAD is dominated by 2.5D work — TINs, planar topology, alignments, COGO, parcels — where a full B-rep solids kernel is heavy machinery. CGAL was initially considered as primary but its triangulation/mesh packages are GPL-3, which would force a GPL Core or commercial license (risk R-7, resolved by this amendment).

## Decision
Layered stack:

- **Survey Engine (domain layer):** Boundary, Parcel, Surface, Coordinate, Point Cloud, Alignment, Corridor, QA engines (ADR-0013).
- **Geometry backend (permissive only):** Boost.Geometry (BSL), GEOS (LGPL, dynamic linkage only), Eigen (MPL2), nanoflann (BSD). Custom TIN engine in-house (ADR-0012). Exact/adaptive predicates ported from auracad.
- **Data backbone:** PDAL (BSD, point clouds), GDAL (MIT, GIS), PROJ (MIT-like, CRS).
- **OCCT satellite (secondary):** DWG interop support, future solids, NURBS, 3D site objects, BIM expansion, mechanical extensions. Isolated behind `interop_core`; no OCCT types in survey/geometry public APIs.

## Rejected
- CGAL-primary: GPL exposure into the open Core.
- OCCT-everywhere: B-rep tax on every survey operation; heavyweight open-core dependency.

## Consequences
The open Core can be permissively licensed (ADR-0006). The geometry engine itself becomes part of the moat: every AI correction, topology fix, and parcel adjustment on it becomes flywheel training data (ADR-0017).
