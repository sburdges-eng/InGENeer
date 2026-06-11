# ADR-0024: Stage 2 Component Triage (auracad + legacy orchestrator)

**Status:** Accepted  
**Date:** 2026-06-11  
**Deciders:** Architecture session (Phase 2.5)  
**Related:** MIGRATION_PLAN Stage 2, ADR-0021 (Apache-2.0 Core)

## Context

Stage 2 knowledge extraction surveyed auracad and legacy InGENeer orchestrator to decide promote vs rewrite before Phase 3 (`audit_core`) and Phase 5 (`geometry_core`).

## Decision — Promote

| Component | Source | Target | Notes |
|-----------|--------|--------|-------|
| Numeric policy header | auracad `numeric_policy.h` | `libs/geometry_core` | Verbatim copy |
| Predicates (orient2d + adaptive 3d when merged) | auracad `predicates.*` | `libs/geometry_core` | Complete 2.5b/c first |
| COGO math/compute | auracad `cogo_*` | `libs/survey_core` / geometry | Align tolerances to policy |
| CXX agentic rules | auracad `CXX_AGENTIC_RULES.md` | Governance + CMake presets | Merge with plan §3.7 |
| SHA-256 audit semantics | TOTaLi + InGENeer `audit.py` | `libs/audit_core` | **Not** auracad FNV-64 |
| Intent phase model | InGENeer orchestrator | `ai_core` interfaces + closed runtime | Four-phase pattern preserved |

## Decision — Rewrite (design only)

| Component | Reason |
|-----------|--------|
| auracad `CryptoHashChain` (FNV-64) | Does not meet chain-of-custody bar; use SHA-256 JSONL |
| auracad ECS / CAD model / Qt UI | Greenfield InGENeer desktop + entity authority model |
| TOTaLi Python pipeline modules | Oracle via fixtures; no Python geometry in open Core |
| LLM intent generator | Closed Aura Intelligence only |

## Decision — Evaluate at Stage 3

- InGENeer `orchestrator/` relocation to `tools/orchestrator/` (ADR-0022 Stage 3)
- Bridge spike `icad-addin/` → `apps/desktop/` integration path

## Consequences

- Phase 3 starts with audit design from TOTaLi/InGENeer, not auracad SQLite hash.
- Phase 5 blocked until predicate 2.5b/c state confirmed on auracad main or cherry-picked.

## References

- `research/auracad/README.md`
- `research/ingeneer-legacy/orchestrator-lessons.md`
