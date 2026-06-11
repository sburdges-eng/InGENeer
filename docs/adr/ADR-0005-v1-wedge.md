# ADR-0005 — V1 Wedge: Topo Field-to-Finish + Legal Descriptions

**Status:** Accepted (D5).

## Decision
Two-pronged v1: the **topographic field-to-finish pipeline** (field data → surface → linework → annotated topo sheet) is the automation spine — highest volume, most automatable, plays to point-cloud strengths. The **legal-description engine** (parcel ↔ metes-and-bounds text) is the narrow, fast-shippable, revenue-capable sidecar with strong LLM fit.

## Rejected
- Boundary/ALTA first: highest per-job value but heaviest legal-reasoning load and PLS judgment; comes after the spine exists.
- Single-prong options: spine-only delays revenue; legals-only doesn't build the platform.

## Consequences
Roadmap sequencing: pointcloud_core → surface_core (TIN) → linework/drafting → sheets, with legal_ai + parcel_core advancing in parallel. Boundary/ALTA workflows build on both.
