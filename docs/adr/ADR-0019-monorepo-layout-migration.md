# ADR-0019 — Monorepo Layout & Five-Stage Migration

**Status:** Accepted. Layout intended to survive a decade.

## Layout (Stage 1 — nothing else at top level)

```
ingeneer/
  apps/
    desktop/
  libs/
    survey_core/  geometry_core/  parcel_core/  surface_core/  pointcloud_core/
    coordinate_core/  interop_core/  ai_core/  audit_core/
  research/
    jepa/  boundary_ai/  legal_ai/
  docs/
    adr/  architecture/
  tools/
```

## Stages

1. **Create monorepo.** InGENeer repo becomes the root with exactly `apps/ libs/ research/ docs/ tools/`.
2. **Import knowledge, not code.** Extract requirements, architecture, data models, experiments, lessons learned from TOTaLi/auracad/old InGENeer into ADRs and `research/`. Old repos contain valuable ideas in unwanted architecture — take the first, not the second.
3. **Promote proven components.** Migrate code only if tested, documented, and architecturally compatible. Otherwise rewrite.
4. **Build adapters.** Temporary bridges (e.g., old point-cloud parser → adapter → new survey core) keep workflows alive during transition.
5. **Delete adapters; archive old repos.**

## Migration triage

- **Keep (knowledge):** survey research, workflow analysis, JEPA research, boundary-logic research, architecture notes, datasets, prompt libraries.
- **Evaluate carefully:** Civil3D REPL, automation bridges, geometry experiments, import pipelines.
- **Probably rewrite (foundational — want them clean):** core CAD, core geometry, core storage, core AI runtime, core data model.

## Rule (R9)

> Migration strategy: strangler pattern. Migrate knowledge first. Migrate code second. Rewrite foundations when uncertain.

## Consequences
TOTaLi stays shippable as the reference oracle until engine parity; auracad and old InGENeer code become archives at Stage 5; existing InGENeer top-level content (orchestrator/, icad-addin/, schemas/) remains as **legacy exceptions** until Stage 3 exit per [ADR-0022](ADR-0022-stage1-relocation-ruling.md); Stage 1a (governance + scripts + skeletons) complete 2026-06-11.
