# Migration Plan — Strangler, Knowledge-First

**Status:** Approved 2026-06-11. Execution requires implementation authorization (currently ON HOLD).
**Rule (R9):** Migrate knowledge first. Migrate code second. Rewrite foundations when uncertain.

## Stage 1 — Create monorepo
InGENeer repo becomes the monorepo root. Top level becomes exactly: `apps/ libs/ research/ docs/ tools/`. Nothing else. (Existing `orchestrator/`, `icad-addin/`, `schemas/` are relocated into the layout during Stages 2–3, not deleted.)

## Stage 2 — Import knowledge, not code
Extract from TOTaLi, auracad, and legacy InGENeer into ADRs and `research/`: requirements, architecture, data models, experiments, lessons learned. Old repos contain valuable ideas inside unwanted architecture — take the ideas.

Knowledge inventory to preserve: survey research, workflow analysis, JEPA research, boundary-logic research, architecture notes, datasets, prompt libraries; TOTaLi's seven invariants (subsumed into the Entity Authority System); auracad's numeric policy, predicate research, audit-chain design, COGO semantics, CXX agentic rules.

## Stage 3 — Promote proven components
Migrate code only if **tested + documented + architecturally compatible**; otherwise rewrite.

| Candidate | Source | Disposition |
|-----------|--------|-------------|
| Exact/adaptive predicates (Shewchuk-style) | auracad/geom | Promote (foundation for TIN engine) |
| Audit chain (SHA-256, append-only) | auracad/core | Promote design; re-implement in audit_core with authority extensions |
| COGO math | auracad/geom | Promote |
| Numeric policy constants | auracad | Promote verbatim |
| OCCT wrapper | auracad/geom | Relocate into interop_core satellite |
| DWG subprocess bridge pattern | auracad/bridge | Promote pattern; codec replaced by ODA |
| TOTaLi pipeline (geodetic, segmentation, extraction, shield, lint) | TOTaLi | **Reference oracle** — keeps shipping; new engines validate against it; port logic selectively |
| Civil3D REPL, automation bridges, import pipelines | TOTaLi/InGENeer | Evaluate carefully |
| Orchestrator phase/envelope logic | InGENeer | Evaluate carefully (ai_core candidate) |
| Qt6 UI | auracad/ui | Do not migrate (ADR-0016) |
| ECS storage, core CAD/geometry/storage/AI-runtime/data model | auracad | **Probably rewrite** — foundational, want them clean |

## Stage 4 — Build adapters
Temporary bridges keep workflows alive (e.g., old point-cloud parser → adapter → new survey core). Adapters are explicitly marked temporary.

## Stage 5 — Delete adapters, archive old repos
TOTaLi and auracad become read-only archives once parity is validated.

## Oracle discipline
TOTaLi semantics are frozen for oracle purposes (A-10); any TOTaLi change during migration requires a drift report.
