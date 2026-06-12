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

## Phase 6.5 finding (2026-06-12) — sixth upstream defect

6. **incircle_2d Layer-A permanent computed from cancelled minors** (auracad main, same
   wrapper lineage as #3): the filter's `permanent` was
   `alift*|bc| + blift*|ca| + clift*|ab|` — absolute values of the already-CANCELLED 2x2
   minors — instead of Shewchuk's sum of absolute products
   `(|bdx·cdy|+|bdy·cdx|)·alift + (|cdx·ady|+|cdy·adx|)·blift + (|adx·bdy|+|ady·bdx|)·clift`.
   The rounding error of each minor scales with the PRODUCT magnitudes, not the cancelled
   difference, so the bound can be arbitrarily (observed: ~12 orders of magnitude) too
   small and Layer A certifies WRONG float signs without ever reaching the exact layers;
   the same too-small permanent also feeds incircle_adapt's B/C thresholds. Found by the
   InGENeer Phase 6.5 TIN debug audit under coverage-guided fuzzing; confirmed against
   exact rational arithmetic. Two pinned repro vectors (see
   `libs/geometry_core/tests/test_predicates.cpp`,
   `test_incircle_filter_permanent_regression`):
   - near-cocircular `a=(4.5,6.5) b=(3.6363636363636362,7.3636363636363633) c=(5,6)
     d=(7,4)`: exact det −2.22e-15 (outside); defective filter certified inside.
   - large-coordinate `a=(7,4) b=(6,7) c=(4,6) d=(−38547229639.746628,
     445103252.0400967)`: exact det −1.04e+22; float det +3.0e+24 — wrong in sign AND
     magnitude — certified by a bound of 3.8e+17 (correct bound: 1.7e+26).
   InGENeer fix (2026-06-12): permanent computed from un-cancelled absolute products.
   Note the Phase 5 10M-iteration soak did NOT catch this: its invariants
   (antisymmetry/cyclic/duplicate/membership) are sign-consistent under a wrong filter
   bound; catching it requires an exact-arithmetic cross-check or a downstream
   structural consumer (the TIN audit). auracad should fix the same expression and add
   the two vectors.
