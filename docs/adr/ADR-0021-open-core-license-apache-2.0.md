# ADR-0021 — Open Core License: Apache-2.0

**Status:** Accepted (closes open question in ADR-0006).  
**Date:** 2026-06-11  
**Related:** [ADR-0006](ADR-0006-market-open-core.md), [CONSTRAINTS.md](../architecture/CONSTRAINTS.md) C-2.1–C-2.3.

## Context

ADR-0006 fixed the open/closed product seam but left the Core OSS license as TBD among Apache-2.0, BSD, and MPL. Phase 1 requires a selection before Stage 1 monorepo restructure and public `libs/` publication.

## Decision

License **AuraCAD Core** (`libs/`, open plugin SDK, open format/automation APIs) under **Apache License 2.0**.

Apply `LICENSE` (Apache-2.0 full text) at `libs/` root when that tree is created in Stage 1; until then, mark new open-boundary modules in documentation as Apache-2.0 intended.

## Comparison (why not the other candidates)

| License | Pros | Why not chosen |
|---------|------|----------------|
| **Apache-2.0** | Permissive; explicit **patent grant** and termination on litigation; standard for platforms (LLVM ecosystem, many CNCF projects); corporate-friendly | Slightly longer license text |
| BSD-2/3-Clause | Minimal text | **No express patent grant** — weaker for a CAD platform inviting commercial forks |
| MPL-2.0 | File-level copyleft balances sharing | **Copyleft on Core files** discourages silent proprietary forks of engines competitors rely on; complicates mental model vs permissive deps (CDT MPL-2.0 is a **dependency**, not the platform license) |

## Dependency compatibility (C-2.1)

| Dependency class | License | Compatible with Apache Core? |
|------------------|---------|------------------------------|
| GEOS | LGPL (dynamic link only) | Yes — dynamic linkage per C-2.1 |
| CDT (surface) | MPL-2.0 | Yes — separate library; comply with MPL for CDT sources if distributed |
| Eigen, nanoflann, Boost.Geometry | MPL2 / BSD / BSL | Yes |
| ODA SDK | Proprietary (member) | **Excluded** from Core — closed bridge per ADR-0020 |
| GPL (libredwg, etc.) | GPL | **Rejected** — C-2.1 |

## Rejected

- GPL for Core (ADR-0006).
- MPL-2.0 as the **platform** license (copyleft scope too broad for stated adoption goal).
- Proprietary Core (contradicts D7 open seam).

## Consequences

- New `libs/*` modules ship with SPDX `Apache-2.0` headers.
- CLA not required for v1 (Apache-2.0 does not require contributor agreements; revisit if corporate contributors demand it).
- Closed **Aura Intelligence** and **ODA bridge** remain proprietary; they interact via process/API boundary (C-2.4).
- Phase 2 CI adds license-allowlist scan: Apache-2.0-compatible in `libs/`; flag GPL static links; flag ODA artifacts.
