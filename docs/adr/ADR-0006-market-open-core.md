# ADR-0006 — Dual Market & Open-Core Licensing Boundary

**Status:** Accepted (D6, D7). Final OSS license TBD (Apache-2.0 / BSD / MPL candidate — open question).

## Decision
**Market:** solo PLS / small firms (1–20 seats) first for adoption velocity; mid/large firms and enterprise second.

**Open — AuraCAD Core:** geometry engine, survey engine, coordinate systems, TIN engine, point-cloud engine, file formats, plugin SDK, automation API.

**Closed — Aura Intelligence:** Spatial JEPA, Survey Foundation Models, agent system, AI copilot, auto-drafting, auto-QC, auto-annotation, cloud services, enterprise features. Plus the ODA DWG codec module (ADR-0018).

Closed components interact with the open Core across process/API boundaries — they never link Core authority internals.

## Rejected
- Proprietary-only: forfeits trust/community leverage in a market dominated by closed incumbents.
- GPL Core: unnecessary once CGAL was dropped (ADR-0011); permissive licensing widens adoption.

## Consequences
The Core/Intelligence seam is the platform's most important interface and must align exactly with the module graph. Forkability risk (R-3) accepted; moat = decisions flywheel (ADR-0017) + engine quality velocity.
