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

## Phase 5 port findings (2026-06-11) — UPSTREAM DEFECTS, fixed in InGENeer only

The port into `libs/geometry_core` (owned code) found and fixed five defects. Per scope
discipline these are flagged here for auracad, NOT fixed upstream:

1. **orient3d_adapt Layer-C structurally incomplete** (branch `agentic/predicates-adaptive-3d-2026-04-24`):
   the hand-enumerated term cascade omits the head·tail·tail cross terms (Shewchuk covers
   them via the bct/bctt expansions) and computes second-order terms as ROUNDED 2-component
   products (the in-code claim that predicates.c approximates these is false — Shewchuk's
   Two_One_Product is exact). Behavioral proof: `orient3d(b,a,c,d)` with a duplicate point
   returns ±1 where every argument order must return 0; antisymmetry violations on 837/1296
   of an exhaustive duplicate-point sweep. InGENeer fix: replaced Layer-C with a full exact
   determinant over 2-component (tail, head) difference entries.
2. **incircle_adapt Layer-C inexact** (same branch): rounded scalar pre-products
   (`scale_a * adxtail`) and rounded cofactor-estimate heads (`bc[3]+bc[2]+bc[1]+bc[0]`).
   Same fix.
3. **orient2d wrapper pivot mismatch** (auracad main, "battle-tested" 2.5a): the public
   wrapper pivots on `a` while orient2d_adapt pivots on `c`; `det_sum` therefore scales the
   adaptive Layer-B/C error bounds with the WRONG product magnitudes, certifying wrong
   signs (~0.5% of near-degenerate uniform fuzz inputs; cyclic-rotation inconsistencies).
   Fix: wrapper now pivots on `c`, exactly like Shewchuk's orient2d.
4. **FP contraction unguarded** (build-level): Apple clang's default `-ffp-contract=on`
   fuses the Layer-A filter determinants and two_product error terms into FMAs, silently
   breaking exactness (observed: incircle Layer-A returned ±1 for three IDENTICAL points).
   InGENeer compiles the predicates TU with `-ffp-contract=off` + `#pragma STDC FP_CONTRACT
   OFF`, and the H-22 gate now verifies the flag in compile_commands.json. auracad should
   audit its build for the same hazard.
5. **Validity domain undocumented**: like reference predicates.c, exactness requires no
   underflow; coordinates perturbed into subnormals break the guarantee. Now documented in
   the InGENeer header; auracad's header carries no such caveat.

Verification after fixes: 10,000,000-iteration deterministic fuzz (uniform + near-degenerate
+ exact-degenerate + 1-ulp perturbed; antisymmetry/cyclic/duplicate/membership invariants):
**zero failures**.
