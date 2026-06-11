# auracad — Predicate Research State

**Status:** Extracted 2026-06-11 (Phase 2.5.2)  
**Source:** `~/Dev/auracad/geom/` + `docs/KERNEL_ROADMAP.md`  
**Stage 3 disposition:** **Promote** Shewchuk-style predicates → `libs/geometry_core` (Phase 5)

## File map

| File | Role |
|------|------|
| `geom/include/aura/geom/predicates.h` | Public API |
| `geom/src/predicates.cpp` | Filter + adaptive implementation (~736 lines) |
| `tests/harness/predicates_test.cpp` | Regression |
| `docs/plans/PHASE_2_5_BC_PLAN.md` | Spec for orient3d / incircle adaptive |

## State matrix (main branch snapshot)

| Predicate | Phase | Status | Notes |
|-----------|-------|--------|-------|
| `orient2d` | 2.5a | **Done** | Filter → Shewchuk adaptive cascade |
| `orient3d` | 2.5b | **TODO** | Filtered only; can return `INDETERMINATE` |
| `incircle_2d` | 2.5c | **TODO** | Filtered only; can return `INDETERMINATE` |

Shared infrastructure staged: Dekker split, expansion arithmetic, error bounds (`kCcwErrBoundA`).

## In-flight work

Branch `agentic/predicates-adaptive-3d-2026-04-24` (auracad) reportedly completes 2.5b/2.5c. **Before Phase 5 port:** verify branch merged or cherry-pick adaptive orient3d/incircle into extraction notes with test vectors.

## Phase 5 acceptance gates

- Predicate fuzz: 1e7 random + adversarial near-degenerate; zero failures
- Cross-check vs reference `predicates.c` outputs on degenerate fixtures
- No `INDETERMINATE` on production TIN insertion paths after adaptive completion
