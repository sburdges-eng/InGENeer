# auracad — Numeric Policy Extraction

**Status:** Extracted 2026-06-11 (Phase 2.5.2)  
**Source:** `~/Dev/auracad/geom/include/aura/geom/numeric_policy.h`  
**Stage 3 disposition:** **Promote verbatim** → `libs/geometry_core/include/.../numeric_policy.h`

## Constants (copy verbatim)

| Constant | Value | Purpose |
|----------|-------|---------|
| `EPSILON` | `1e-12` | Tight geometric equality |
| `LENGTH_EPSILON` | `1e-9` | Length comparisons (mm-scale CAD) |
| `ANGLE_EPSILON_DEG` | `1e-6` | Angle equality (degrees) |
| `DETERMINANT_EPSILON` | `1e-15` | Filter threshold; below → exact predicate required |
| `kRobustPredicatesRequired` | `true` | Compile-time contract |

## Hard rules (from header + CXX_AGENTIC_RULES §2.3)

- `static_assert(sizeof(double) == 8)` and `is_iec559` — binary64 only
- **Forbidden:** `-ffast-math`, `-Ofast`, `-ffinite-math-only`, `-freciprocal-math`
- Robust predicates **before** tolerance fallbacks

## Change protocol

Any constant change requires:

1. Update `docs/architecture/` invariant cross-ref (was `PRODUCTION_DESIGN_ALIGNMENT.md` §2 in auracad)
2. Full geometry regression suite
3. Commit message justification (public ABI for kernel correctness)

## Tests to port

- `auracad/tests/harness/numeric_policy_test.cpp` — locks values + tolerance ladder (`DETERMINANT < EPSILON < LENGTH`)

## Known debt

Some `cogo_compute.cpp` paths use hardcoded `1e-12` instead of policy constants — align during COGO port.
