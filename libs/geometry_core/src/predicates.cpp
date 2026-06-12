// SPDX-License-Identifier: Apache-2.0
//
// src/predicates.cpp -- robust predicates: filtered double + adaptive
// multi-precision (orient2d, orient3d, incircle_2d).
//
// Ported from auracad geom (branch agentic/predicates-adaptive-3d-2026-04-24)
// per ADR-0024; Shewchuk robust predicates (public domain,
// https://www.cs.cmu.edu/~quake/robust.html).
//
// See include/ingeneer/geom/predicates.h for the contract this file
// implements. orient2d, orient3d, and incircle_2d each use an adaptive
// cascade (filter -> one-level adapt -> full doubly-adaptive) and never
// return INDETERMINATE on finite inputs (Shewchuk §4.4 / §4.5 / §4.6).
// on_segment_2d / bbox_overlap_2d remain filtered-only.
//
// In-body comments referencing auracad phase numbers (Phase 2.5a/b/c) are
// retained verbatim from the source for provenance and cross-auditing.
//
// Adaptive arithmetic credits
// ---------------------------
// The expansion-arithmetic primitives (Two_Sum, Fast_Two_Sum, Two_Diff,
// Two_Product via Dekker split, expansion_sum_zeroelim,
// scale_expansion_zeroelim, estimate) and the orient2dadapt cascade are
// direct ports of the algorithms in:
//
//   Jonathan R. Shewchuk, "Adaptive Precision Floating-Point Arithmetic
//   and Fast Robust Geometric Predicates", Discrete & Computational
//   Geometry 18:305-363, 1997. Reference C source `predicates.c` is
//   placed in the public domain by the author.
//
// Implementation notes:
//
//   * No std::fma. We use Dekker's split (Shewchuk §3.3) for Two_Product
//     so the implementation is bit-reproducible across compilers and CPUs
//     that may or may not enable hardware FMA. This matches the
//     determinism contract in docs/PRODUCTION_DESIGN_ALIGNMENT.md §2.
//
//   * No SIMD intrinsics, no compiler-specific reassociation pragmas.
//
//   * No -ffast-math: every operation here assumes IEEE 754 binary64
//     semantics. The static_assert in numeric_policy.h guards against
//     accidental builds with that flag. Note the `volatile` casts inside
//     the expansion primitives -- they prevent the compiler from
//     algebraically simplifying away the very rounding errors we are
//     trying to capture.
//
//   * Filtered error bounds use a cheap upper-bound style derived from
//     forward error analysis on the determinant expansion. They are
//     intentionally conservative -- when the filter cannot decide, the
//     predicate dispatches to the adaptive cascade (orient2d_adapt,
//     orient3d_adapt, incircle_adapt). As of Phase 2.5c, all three
//     adaptive predicates return INDETERMINATE only on non-finite input.

#include "ingeneer/geom/predicates.h"

#include <cmath>
#include <cstdlib>
#include <limits>

// FP contraction MUST be off in this translation unit (plan H-22, C-4.2): FMA
// contraction silently breaks two_product/two_diff error-term derivations and the
// Layer-A filters (observed: incircle Layer-A returned 2.4e-18 for three identical
// points under Apple clang's default -ffp-contract=on). The build also passes
// -ffp-contract=off for this target; the pragma protects ad-hoc builds.
#pragma STDC FP_CONTRACT OFF

namespace ingeneer::geom::predicates {

namespace {

// One ULP of binary64 mantissa = 2^-52. Pre-evaluated as a compile-time
// constant so the error bound expression below has no implicit
// numeric_limits dependency at runtime.
inline constexpr double kDoubleEps = std::numeric_limits<double>::epsilon();

// ---- Shewchuk error-bound coefficients (orient2d adaptive cascade) -----
//
// Derived in Shewchuk 1997 §4.4. With `epsilon = 2^-53` (binary64 unit
// roundoff = kDoubleEps / 2), the published constants are:
//
//   resulterrbound   = (3 + 8*eps)   * eps
//   ccwerrboundA     = (3 + 16*eps)  * eps     // filter (already used)
//   ccwerrboundB     = (2 + 12*eps)  * eps     // layer-B adapt threshold
//   ccwerrboundC     = (9 + 64*eps)  * eps^2   // layer-C adapt threshold
//
// We compute them here at constexpr time so the cascade has zero startup
// cost (Shewchuk's reference impl computes these in exactinit()).
//
// Note: Shewchuk's `epsilon` is the unit roundoff (half an ULP),
// i.e. 2^-53. C++'s std::numeric_limits<double>::epsilon() returns the
// machine epsilon (one ULP at 1.0), i.e. 2^-52. So Shewchuk's `epsilon`
// equals our `kDoubleEps / 2`.
inline constexpr double kShewchukEps = kDoubleEps * 0.5;  // 2^-53
inline constexpr double kCcwErrBoundA = (3.0 + 16.0 * kShewchukEps) * kShewchukEps;
inline constexpr double kCcwErrBoundB = (2.0 + 12.0 * kShewchukEps) * kShewchukEps;
inline constexpr double kCcwErrBoundC = (9.0 + 64.0 * kShewchukEps) * kShewchukEps * kShewchukEps;
// Shewchuk's `resulterrbound`. Used to scale the layer-C cascade's final
// |det| for the residual-error test. Named here so Phase 2.5b/c (orient3d,
// incircle adaptive cascades) can reuse without re-declaring.
inline constexpr double kResultErrBound = (3.0 + 8.0 * kShewchukEps) * kShewchukEps;

// ---- Shewchuk error-bound coefficients (orient3d adaptive cascade) -----
//
// Derived in Shewchuk 1997 §4.5. With `epsilon = 2^-53` (binary64 unit
// roundoff = kShewchukEps):
//
//   o3derrboundA = (7 + 56*eps)  * eps     // layer A static filter
//   o3derrboundB = (3 + 28*eps)  * eps     // layer B adapt threshold
//   o3derrboundC = (26 + 288*eps) * eps^2  // layer C adapt threshold
//
// The published constants use Shewchuk's `epsilon` (= 2^-53 = kShewchukEps).
inline constexpr double kO3dErrBoundA = (7.0 + 56.0 * kShewchukEps) * kShewchukEps;
inline constexpr double kO3dErrBoundB = (3.0 + 28.0 * kShewchukEps) * kShewchukEps;
// Layer-C intermediate threshold for orient3d. Shewchuk's orient3dadapt
// uses this after the second-order correction batch: if the expansion
// estimate's magnitude exceeds kO3dErrBoundC * permanent at that point, the
// sign is already certified and the triple-tail section can be skipped.
[[maybe_unused]] inline constexpr double kO3dErrBoundC =
    (26.0 + 288.0 * kShewchukEps) * kShewchukEps * kShewchukEps;

// Maximum expansion length for orient3d layer-C intermediate buffers.
// Shewchuk §4.5 derives the worst-case final expansion as 192 components.
// Size derivation (following Shewchuk §4.5 Tables 7-8 and predicates.c):
//   Each of the 3 input sub-differences (e.g. adx = a.x - d.x) decomposes
//   via two_diff into a (head, tail) pair. Each of the three 2x2 minors
//   (e.g. bdey = bdy*bdz - bdz*bdy) involves two two_product calls (each
//   producing a 2-component expansion) combined via two_two_diff into a
//   4-component expansion. The full Layer-B "det" expansion is formed by
//   summing three 4-component expansions scaled by three scalar row elements,
//   which under scale_expansion_zeroelim produces up to 8 components per
//   term; three such terms summed via expansion_sum_zeroelim reach at most
//   24 components. Layer-C then adds correction terms built from the input
//   tails (axby, axbz, ...) — each correction batch is again 4 components,
//   and successive expansion_sum_zeroelim accumulations grow the running
//   expansion. Shewchuk's reference code allocates up to 192 components for
//   the final expansion (see predicates.c Temp4[192] / Temp8[192]). We use
//   192 uniformly across all intermediate buffers for simplicity.
inline constexpr int kO3dMaxExpansion = 192;

// ---- Shewchuk error-bound coefficients (incircle_2d adaptive cascade) -----
//
// Derived in Shewchuk 1997 §4.6. With `epsilon = 2^-53` (binary64 unit
// roundoff = kShewchukEps):
//
//   iccerrboundA = (10 + 96*eps)  * eps     // layer A static filter
//   iccerrboundB = (4  + 48*eps)  * eps     // layer B adapt threshold
//   iccerrboundC = (44 + 576*eps) * eps^2   // layer C adapt threshold
//
// The published constants use Shewchuk's `epsilon` (= 2^-53 = kShewchukEps).
inline constexpr double kIccErrBoundA = (10.0 + 96.0 * kShewchukEps) * kShewchukEps;
inline constexpr double kIccErrBoundB = (4.0 + 48.0 * kShewchukEps) * kShewchukEps;
// Layer-C intermediate threshold for incircle. Shewchuk's incircleadapt uses
// this after the second-order correction batch: if the expansion estimate's
// magnitude exceeds kIccErrBoundC * permanent at that point, the sign is
// already certified and the further correction terms can be skipped.
[[maybe_unused]] inline constexpr double kIccErrBoundC =
    (44.0 + 576.0 * kShewchukEps) * kShewchukEps * kShewchukEps;

// Maximum expansion length for incircle_2d layer-C intermediate buffers.
// Shewchuk §4.6 derives the worst-case final expansion as 1152 components.
// Size derivation (following Shewchuk §4.6 and predicates.c):
//   The incircle determinant is degree 4 in the input coordinates (vs. degree
//   3 for orient3d). Each coordinate difference decomposes via two_diff into a
//   (head, tail) pair. Each of the 3 exact 2x2 cofactors is a 4-component
//   expansion (two_product + two_two_diff). Scaling each 4-comp cofactor by a
//   scalar squared-lift via scale_expansion_zeroelim yields 8 components per
//   term; three such terms summed reach at most 24 components (Layer B).
//   Layer C then adds correction terms for:
//     - The squared-lift accuracy corrections (adxtail, adytail → axax0, ayay0
//       from two_product): scaling 4-comp cofactors by the 2-comp squared-lift
//       tails yields up to 16 components per group.
//     - The inner cofactor tail corrections (6 groups of pairs, each yielding
//       up to 8 components per scale step).
//     - Second-order corrections (product of two tails times a head), each
//       2-component.
//     - Third-order (triple-tail) corrections, each up to 4 components.
//   The sum of all these accumulated via expansion_sum_zeroelim grows the
//   running buffer. Shewchuk's reference code allocates 1152 components for
//   the final incircle expansion (see predicates.c, incircleadapt buffers).
//   We use kIccMaxExpansion uniformly across all intermediate buffers.
inline constexpr int kIccMaxExpansion = 1152;

// Splitter for Dekker's split (Shewchuk §3.3). For binary64 with
// 53-bit mantissa, splitter = 2^27 + 1.
inline constexpr double kSplitter = 134217729.0;  // 2^27 + 1

// ---- Expansion arithmetic (Shewchuk §2 / §3) ---------------------------
//
// Each primitive returns an exact 2-component non-overlapping expansion
// `(hi, lo)` such that `hi + lo` equals the true result of the operation
// at infinite precision. The `volatile` casts on the intermediates
// matter: they prevent the compiler from reasoning that `(a + b) - a` is
// algebraically `b` and folding away the captured rounding error.

// Two_Sum: exact 2-component sum. Knuth, TAOCP 4.2.2 / Shewchuk §2.
// Returns (sum, error) such that sum + error == a + b at infinite
// precision, with sum == fl(a + b) and |error| <= ulp(sum) / 2.
inline void two_sum(double a, double b, double& sum, double& err) noexcept {
    sum = a + b;
    const volatile double bvirt_v = sum - a;
    const double bvirt = bvirt_v;
    const volatile double avirt_v = sum - bvirt;
    const double avirt = avirt_v;
    const double bround = b - bvirt;
    const double around = a - avirt;
    err = around + bround;
}

// Fast_Two_Sum: exact 2-component sum, assuming |a| >= |b|. Saves one
// floating-point op vs. two_sum. Caller is responsible for the magnitude
// precondition; using fast_two_sum without it produces a non-canonical
// expansion. Shewchuk §2 / Dekker 1971.
inline void fast_two_sum(double a, double b, double& sum, double& err) noexcept {
    sum = a + b;
    const volatile double bvirt_v = sum - a;
    const double bvirt = bvirt_v;
    err = b - bvirt;
}

// Two_Diff: exact 2-component difference. Same structure as two_sum but
// for subtraction. Shewchuk §2.
inline void two_diff(double a, double b, double& diff, double& err) noexcept {
    diff = a - b;
    const volatile double bvirt_v = a - diff;
    const double bvirt = bvirt_v;
    const volatile double avirt_v = diff + bvirt;
    const double avirt = avirt_v;
    const double bround = bvirt - b;
    const double around = a - avirt;
    err = around + bround;
}

// Split: Dekker's split. Decomposes a binary64 value into a high- and
// low-half such that `hi + lo == a` exactly and the two halves have at
// most 26 significant bits each (so their product has at most 52 bits
// and fits in a single binary64). Shewchuk §3.3, Theorem 17.
inline void split(double a, double& hi, double& lo) noexcept {
    const double c = kSplitter * a;
    const volatile double abig_v = c - a;
    const double abig = abig_v;
    hi = c - abig;
    lo = a - hi;
}

// Two_Product: exact 2-component product via Dekker's split. Returns
// (prod, error) such that prod + error == a * b at infinite precision.
// Shewchuk §3.3. We deliberately use Dekker's split rather than std::fma
// for cross-compiler / cross-CPU reproducibility (see file docblock).
inline void two_product(double a, double b, double& prod, double& err) noexcept {
    prod = a * b;
    double ahi, alo, bhi, blo;
    split(a, ahi, alo);
    split(b, bhi, blo);
    const double err1 = prod - (ahi * bhi);
    const double err2 = err1 - (alo * bhi);
    const double err3 = err2 - (ahi * blo);
    err = (alo * blo) - err3;
}

// expansion_sum_zeroelim: sum two non-overlapping expansions e (length
// elen) and f (length flen) into a non-overlapping expansion h (length
// hlen, returned). Eliminates zero components on the fly so h has no
// zero entries (except the trivial case where the true sum is zero, in
// which case hlen == 1 and h[0] == 0).
//
// This is Shewchuk's `fast_expansion_sum_zeroelim` (Figure 23 in §2.7),
// which requires both inputs to be sorted in increasing order of
// magnitude -- a property that holds for the expansions we feed it
// (results of two_product / scale_expansion / earlier expansion sums).
//
// h must have capacity >= elen + flen.
inline int expansion_sum_zeroelim(int elen, const double* e, int flen, const double* f,
                                  double* h) noexcept {
    int eindex = 0;
    int findex = 0;
    double enow = e[0];
    double fnow = f[0];
    double q;
    if ((fnow > enow) == (fnow > -enow)) {
        q = enow;
        ++eindex;
        if (eindex < elen) enow = e[eindex];
    } else {
        q = fnow;
        ++findex;
        if (findex < flen) fnow = f[findex];
    }
    int hindex = 0;
    double hh;
    double qnew;
    if ((eindex < elen) && (findex < flen)) {
        if ((fnow > enow) == (fnow > -enow)) {
            fast_two_sum(enow, q, qnew, hh);
            ++eindex;
            if (eindex < elen) enow = e[eindex];
        } else {
            fast_two_sum(fnow, q, qnew, hh);
            ++findex;
            if (findex < flen) fnow = f[findex];
        }
        q = qnew;
        if (hh != 0.0) {
            h[hindex++] = hh;
        }
        while ((eindex < elen) && (findex < flen)) {
            if ((fnow > enow) == (fnow > -enow)) {
                two_sum(q, enow, qnew, hh);
                ++eindex;
                if (eindex < elen) enow = e[eindex];
            } else {
                two_sum(q, fnow, qnew, hh);
                ++findex;
                if (findex < flen) fnow = f[findex];
            }
            q = qnew;
            if (hh != 0.0) {
                h[hindex++] = hh;
            }
        }
    }
    while (eindex < elen) {
        two_sum(q, enow, qnew, hh);
        ++eindex;
        if (eindex < elen) enow = e[eindex];
        q = qnew;
        if (hh != 0.0) {
            h[hindex++] = hh;
        }
    }
    while (findex < flen) {
        two_sum(q, fnow, qnew, hh);
        ++findex;
        if (findex < flen) fnow = f[findex];
        q = qnew;
        if (hh != 0.0) {
            h[hindex++] = hh;
        }
    }
    if ((q != 0.0) || (hindex == 0)) {
        h[hindex++] = q;
    }
    return hindex;
}

// scale_expansion_zeroelim: multiply expansion e (length elen) by scalar
// b, into non-overlapping expansion h (length returned). Eliminates zero
// components on the fly. Shewchuk Figure 21 in §2.6.
//
// h must have capacity >= 2 * elen.
//
// Used by orient3d_adapt to scale the exact 4-component 2x2 cofactor
// expansions by each first-column scalar (adx, ady, adz) and their tails.
inline int scale_expansion_zeroelim(int elen, const double* e, double b, double* h) noexcept {
    double bhi, blo;
    split(b, bhi, blo);
    double q, hh;
    // First component.
    {
        const double enow = e[0];
        double prod, err;
        // Inline two_product but reuse the precomputed (bhi, blo).
        prod = enow * b;
        double ahi, alo;
        split(enow, ahi, alo);
        const double err1 = prod - (ahi * bhi);
        const double err2 = err1 - (alo * bhi);
        const double err3 = err2 - (ahi * blo);
        err = (alo * blo) - err3;
        q = prod;
        hh = err;
    }
    int hindex = 0;
    if (hh != 0.0) {
        h[hindex++] = hh;
    }
    for (int eindex = 1; eindex < elen; ++eindex) {
        const double enow = e[eindex];
        // two_product(enow, b, prod, err) reusing (bhi, blo).
        double prod = enow * b;
        double ahi, alo;
        split(enow, ahi, alo);
        const double err1 = prod - (ahi * bhi);
        const double err2 = err1 - (alo * bhi);
        const double err3 = err2 - (ahi * blo);
        const double err = (alo * blo) - err3;
        // sum with carry q; capture rounding error.
        double sum;
        two_sum(q, err, sum, hh);
        if (hh != 0.0) {
            h[hindex++] = hh;
        }
        // fast_two_sum(prod, sum, qnew, hh) -- prod dominates.
        double qnew;
        fast_two_sum(prod, sum, qnew, hh);
        q = qnew;
        if (hh != 0.0) {
            h[hindex++] = hh;
        }
    }
    if ((q != 0.0) || (hindex == 0)) {
        h[hindex++] = q;
    }
    return hindex;
}

// estimate: sum the components of expansion e to a single double.
// Components are stored in increasing order of magnitude (index 0 = least
// significant, index elen-1 = most significant). We sum in that order —
// matching Shewchuk's reference implementation. The result is a
// floating-point APPROXIMATION of the expansion's value (not an exact
// sum); precision-preserving compensated summation is what the caller
// would invoke separately if needed. Shewchuk §2.4.
inline double estimate(int elen, const double* e) noexcept {
    double q = e[0];
    for (int i = 1; i < elen; ++i) {
        q = q + e[i];
    }
    return q;
}

// ---- Two_One_Diff / Two_One_Sum / Two_Two_Diff / Two_Two_Sum helpers ------
//
// Direct ports of Shewchuk's macros (Figure 7 / 8, §2.6 and §3). Given two
// non-overlapping 2-component expansions {a1, a0} and {b1, b0} (with
// a1, b1 the high components), Two_Two_{Diff,Sum} produce a 4-component
// non-overlapping expansion {x3, x2, x1, x0} (x3 high, x0 low) whose
// sum equals (a1 + a0) ± (b1 + b0) exactly.

// Two_One_Diff: (a1 + a0) - b expanded into a 3-component result.
// Output ordering: x2 high, x0 low.
inline void two_one_diff(double a1, double a0, double b, double& x2, double& x1,
                         double& x0) noexcept {
    // _i = (a0 - b) head with x0 the rounding tail.
    double i;
    two_diff(a0, b, i, x0);
    // x2 = (a1 + _i) head with x1 the rounding tail.
    two_sum(a1, i, x2, x1);
}

// Two_One_Sum: (a1 + a0) + b expanded into a 3-component result.
// Output ordering: x2 high, x0 low.
inline void two_one_sum(double a1, double a0, double b, double& x2, double& x1,
                        double& x0) noexcept {
    double i;
    two_sum(a0, b, i, x0);
    two_sum(a1, i, x2, x1);
}

// Two_Two_Diff: (a1 + a0) - (b1 + b0) expanded into a 4-component result.
inline void two_two_diff(double a1, double a0, double b1, double b0, double& x3, double& x2,
                         double& x1, double& x0) noexcept {
    // Step 1: subtract b0 from {a1, a0}, producing {j, t, x0}.
    double j, t;
    two_one_diff(a1, a0, b0, j, t, x0);
    // Step 2: subtract b1 from {j, t}, producing {x3, x2, x1}.
    two_one_diff(j, t, b1, x3, x2, x1);
}

// Two_Two_Sum: (a1 + a0) + (b1 + b0) expanded into a 4-component result.
// Staged for Phase 2.5c (incircle_2d adaptive cascade).
[[maybe_unused]] inline void two_two_sum(double a1, double a0, double b1, double b0, double& x3,
                                         double& x2, double& x1, double& x0) noexcept {
    double j, t;
    two_one_sum(a1, a0, b0, j, t, x0);
    two_one_sum(j, t, b1, x3, x2, x1);
}

// ---- Exact expansion-algebra helpers (full-exact Layer-C fallbacks) -----
//
// Added 2026-06-11, replacing the upstream auracad Layer-C term cascades,
// which fuzzing showed to be structurally incomplete and partially inexact
// (see the Step-3 comments in orient3d_adapt / incircle_adapt). Every helper
// here is exact: no rounding is ever discarded.

// h = e * f (expansion x expansion), exactly: sum of per-component scalings.
// h must hold >= 2*elen*flen doubles and must not alias e/f/work; work must
// be at least as large as h. All callers keep elen <= 16 (term buffer 32).
inline int expansion_product(int elen, const double* e, int flen, const double* f, double* h,
                             double* work) noexcept {
    double term[32];
    int hlen = scale_expansion_zeroelim(elen, e, f[0], h);
    for (int i = 1; i < flen; ++i) {
        const int tlen = scale_expansion_zeroelim(elen, e, f[i], term);
        const int nlen = expansion_sum_zeroelim(hlen, h, tlen, term, work);
        for (int k = 0; k < nlen; ++k) h[k] = work[k];
        hlen = nlen;
    }
    return hlen;
}

inline void expansion_negate(int n, double* e) noexcept {
    for (int i = 0; i < n; ++i) e[i] = -e[i];
}

// out = p*q - r*s over 2-component expansions, exactly. out >= 16 doubles.
inline int exact_minor2(const double p[2], const double q[2], const double r[2], const double s[2],
                        double* out) noexcept {
    double pq[8], rs[8], w1[8], w2[8];
    const int pqlen = expansion_product(2, p, 2, q, pq, w1);
    const int rslen = expansion_product(2, r, 2, s, rs, w2);
    expansion_negate(rslen, rs);
    return expansion_sum_zeroelim(pqlen, pq, rslen, rs, out);
}

// out = x^2 + y^2 over 2-component expansions, exactly. out >= 16 doubles.
inline int exact_lift2(const double x[2], const double y[2], double* out) noexcept {
    double xx[8], yy[8], w1[8], w2[8];
    const int xxlen = expansion_product(2, x, 2, x, xx, w1);
    const int yylen = expansion_product(2, y, 2, y, yy, w2);
    return expansion_sum_zeroelim(xxlen, xx, yylen, yy, out);
}

// ---- orient3d adaptive cascade -----------------------------------------
//
// orient3d_adapt is invoked when the filtered (layer-A) estimate failed to
// disambiguate the sign of the 3x3 determinant. Direct port of Shewchuk's
// `orient3dadapt` (predicates.c, §4.5 of the 1997 paper), adapted to use
// this codebase's Point3 type, `a` as the reference vertex (not `d` as in
// Shewchuk), and the 2.5a primitive set.
//
// The determinant we compute is (using `a` as the reference vertex):
//
//   | b.x-a.x  b.y-a.y  b.z-a.z |
//   | c.x-a.x  c.y-a.y  c.z-a.z |   = M
//   | d.x-a.x  d.y-a.y  d.z-a.z |
//
// Variable naming follows Shewchuk's predicates.c with the mapping:
//   adx=bx, ady=by, adz=bz    (b - a in our notation)
//   bdx=cx, bdy=cy, bdz=cz    (c - a in our notation)
//   cdx=dx, cdy=dy, cdz=dz    (d - a in our notation)
//
// Layer B: compute three 2×2 cofactors as 4-component expansions via
//   two_product + two_two_diff. Scale each by its row-0 coefficient via
//   scale_expansion_zeroelim (8 components). Sum the three scaled expansions
//   into a running expansion via expansion_sum_zeroelim. If the estimate
//   clears kO3dErrBoundB * permanent, return.
//
// Layer C: compute all correction terms involving the input-subtraction
//   tails. The cascade is:
//   (1) Outer-tail corrections: scale each 4-comp sub-det by its outer tail.
//   (2) Inner-tail corrections: for each pair of inner tails in a 2x2 minor,
//       build the cross-product scaled by the outer head.
//   (3) Second-order corrections: two-tail × one-head products (12 terms).
//   (4) C-threshold check: if |det_estimate| > kO3dErrBoundC * permanent,
//       return early — sign is certified without triple-tail refinement.
//   (5) Triple-tail corrections: three-tail products (6 terms, Shewchuk §4.5).
//   (6) Return the most-significant component of the final expansion; its
//       sign is guaranteed correct by Shewchuk Lemma 5.
//
// Buffer sizes: all buffers are sized to kO3dMaxExpansion (192).
//
// The returned double's SIGN is the correct orientation. Magnitude has
// no operational meaning.
inline double orient3d_adapt(const Point3& a, const Point3& b, const Point3& c, const Point3& d,
                             double permanent) noexcept {
    // ---- Step 1: compute 9 input-difference heads and tails --------------
    //
    // Each two_diff call produces the rounded fl(p-q) as the head, plus the
    // exact rounding tail. Naming follows Shewchuk predicates.c orient3dadapt
    // with the mapping: adx=b-a, bdx=c-a, cdx=d-a (we use `a` as pivot,
    // not `d` as in Shewchuk's original which uses `d` as pivot).

    double adx, adxtail;
    two_diff(b.x, a.x, adx, adxtail);
    double ady, adytail;
    two_diff(b.y, a.y, ady, adytail);
    double adz, adztail;
    two_diff(b.z, a.z, adz, adztail);
    double bdx, bdxtail;
    two_diff(c.x, a.x, bdx, bdxtail);
    double bdy, bdytail;
    two_diff(c.y, a.y, bdy, bdytail);
    double bdz, bdztail;
    two_diff(c.z, a.z, bdz, bdztail);
    double cdx, cdxtail;
    two_diff(d.x, a.x, cdx, cdxtail);
    double cdy, cdytail;
    two_diff(d.y, a.y, cdy, cdytail);
    double cdz, cdztail;
    two_diff(d.z, a.z, cdz, cdztail);

    // ---- Step 2: Layer-B exact cofactor expansions -----------------------
    //
    // The 3x3 determinant (cofactor expansion along the first column, adx):
    //   adx*(bdy*cdz - bdz*cdy) - ady*(bdx*cdz - bdz*cdx) + adz*(bdx*cdy - bdy*cdx)
    //
    // Each 2x2 cofactor is computed exactly as a 4-component expansion via
    // two_product + two_two_diff, then scaled by the corresponding first-
    // column element via scale_expansion_zeroelim (producing up to 8 comps).

    // Sub-det adet: bdy*cdz - bdz*cdy  (cofactor for adx column)
    double axby[4], axcy[4];  // intermediate 4-comp expansions
    {
        double s1, s0, t1, t0;
        two_product(bdy, cdz, s1, s0);
        two_product(bdz, cdy, t1, t0);
        two_two_diff(s1, s0, t1, t0, axby[3], axby[2], axby[1], axby[0]);
    }

    // Sub-det bdet: bdx*cdz - bdz*cdx  (cofactor for -ady column)
    {
        double s1, s0, t1, t0;
        two_product(bdx, cdz, s1, s0);
        two_product(bdz, cdx, t1, t0);
        two_two_diff(s1, s0, t1, t0, axcy[3], axcy[2], axcy[1], axcy[0]);
    }

    // Sub-det cdet: bdx*cdy - bdy*cdx  (cofactor for adz column)
    double aycx[4];
    {
        double s1, s0, t1, t0;
        two_product(bdx, cdy, s1, s0);
        two_product(bdy, cdx, t1, t0);
        two_two_diff(s1, s0, t1, t0, aycx[3], aycx[2], aycx[1], aycx[0]);
    }

    // Scale each sub-det by the corresponding first-column element.
    // adx * (bdy*cdz - bdz*cdy)
    double adet[8];
    int alen = scale_expansion_zeroelim(4, axby, adx, adet);

    // -ady * (bdx*cdz - bdz*cdx)
    double bdet[8];
    int blen = scale_expansion_zeroelim(4, axcy, -ady, bdet);

    // adz * (bdx*cdy - bdy*cdx)
    double cdet[8];
    int clen = scale_expansion_zeroelim(4, aycx, adz, cdet);

    // Sum into fin1.
    double fin1[kO3dMaxExpansion];
    double fin2[kO3dMaxExpansion];
    {
        double abdet[kO3dMaxExpansion];
        int ablen = expansion_sum_zeroelim(alen, adet, blen, bdet, abdet);
        int finlen = expansion_sum_zeroelim(ablen, abdet, clen, cdet, fin1);

        // ---- Layer-B threshold check ------------------------------------
        double det = estimate(finlen, fin1);
        double errbound = kO3dErrBoundB * permanent;
        if ((det >= errbound) || (-det >= errbound)) {
            return det;
        }

        // ---- Step 3: full exact determinant over 2-component entries --------
        //
        // The upstream auracad Layer-C term cascade was structurally incomplete
        // (missing the head*tail*tail cross terms) and partially inexact
        // (rounded 2-component second-order terms): fuzzing exposed
        // antisymmetry violations and spurious nonzero signs on duplicate-point
        // inputs (2026-06-11; see research/auracad/predicates-state.md). It is
        // replaced by the computation the cascade approximates: the full 3x3
        // determinant over the EXACT 2-component (tail, head) difference
        // entries, using exact expansion products only. This path runs only
        // when Layer B cannot certify the sign - correctness over
        // micro-optimization (C-5.3).
        //
        //   det = Bx*(Cy*Dz - Cz*Dy) - By*(Cx*Dz - Cz*Dx) + Bz*(Cx*Dy - Cy*Dx)
        //
        // with B = b-a, C = c-a, D = d-a, every coordinate an exact 2-comp
        // expansion. Max lengths: minor <= 16, cofactor <= 64, total <= 192
        // (= kO3dMaxExpansion).
        const double ex_bx[2] = {adxtail, adx};
        const double ex_by[2] = {adytail, ady};
        const double ex_bz[2] = {adztail, adz};
        const double ex_cx[2] = {bdxtail, bdx};
        const double ex_cy[2] = {bdytail, bdy};
        const double ex_cz[2] = {bdztail, bdz};
        const double ex_dx[2] = {cdxtail, cdx};
        const double ex_dy[2] = {cdytail, cdy};
        const double ex_dz[2] = {cdztail, cdz};

        double m1[16], m2[16], m3[16];
        const int m1len = exact_minor2(ex_cy, ex_dz, ex_cz, ex_dy, m1);
        const int m2len = exact_minor2(ex_cx, ex_dz, ex_cz, ex_dx, m2);
        const int m3len = exact_minor2(ex_cx, ex_dy, ex_cy, ex_dx, m3);

        double cof1[64], cof2[64], cof3[64], work[64];
        const int c1len = expansion_product(2, ex_bx, m1len, m1, cof1, work);
        const int c2len = expansion_product(2, ex_by, m2len, m2, cof2, work);
        expansion_negate(c2len, cof2);
        const int c3len = expansion_product(2, ex_bz, m3len, m3, cof3, work);

        const int s12len = expansion_sum_zeroelim(c1len, cof1, c2len, cof2, fin2);
        const int exlen = expansion_sum_zeroelim(s12len, fin2, c3len, cof3, fin1);
        return fin1[exlen - 1];
    }
}

// ---- incircle_2d adaptive cascade --------------------------------------
//
// incircle_adapt is invoked when the filtered (Layer-A) estimate failed to
// disambiguate the sign of the 3x3 incircle determinant. Direct port of
// Shewchuk's `incircleadapt` (predicates.c, §4.6 of the 1997 paper),
// adapted to use this codebase's Point2 type and the 2.5a/2.5b primitive
// set. Credit: Jonathan R. Shewchuk, "Adaptive Precision Floating-Point
// Arithmetic and Fast Robust Geometric Predicates", DCG 18:305-363, 1997.
// The reference C source is placed in the public domain by the author.
//
// The determinant we compute is:
//
//   | a.x-d.x   a.y-d.y   (a.x-d.x)^2+(a.y-d.y)^2 |
//   | b.x-d.x   b.y-d.y   (b.x-d.x)^2+(b.y-d.y)^2 |
//   | c.x-d.x   c.y-d.y   (c.x-d.x)^2+(c.y-d.y)^2 |
//
// Expanding along the third column (paraboloid-lift), the three 2x2 cofactors
// are orient2d-shaped expressions. Each is computed exactly as a 4-component
// expansion, then scaled by the corresponding floating-point squared-lift.
//
// Variable naming follows Shewchuk's predicates.c incircleadapt:
//   adx/ady/adxtail/adytail: a - d differences and their two_diff tails
//   bdx/bdy/..., cdx/cdy/...: similarly for b-d and c-d
//   ab = adx*bdy - ady*bdx (4-comp), bc = bdx*cdy - bdy*cdx (4-comp),
//   ca = cdx*ady - cdy*adx (4-comp)
//   alift = fl(adx^2 + ady^2), blift, clift (Layer-B scalar approximations)
//   axax1/axax0: exact two_product(adx, adx) for Layer-C squared corrections
//
// Cascade structure:
//   Layer B: scale each 4-comp cofactor by its scalar squared-lift → 8-comp.
//     Sum three → Layer-B expansion. Threshold vs. kIccErrBoundB * permanent.
//   Layer C: accumulate squared-lift correction terms (axax0, ayay0, etc.),
//     inner-tail correction terms, second-order terms. Threshold vs.
//     kIccErrBoundC * permanent. Accumulate further correction terms.
//     Return sign of most-significant component (Shewchuk Lemma 5).
//
// Buffer sizes: kIccMaxExpansion (1152) for both ping-pong buffers.
// Stack cost: 2 * 1152 * 8 bytes ≈ 18 KB — within typical thread stack.
//
// The returned double's SIGN is the correct incircle sign. Magnitude has
// no operational meaning.
inline double incircle_adapt(const Point2& a, const Point2& b, const Point2& c, const Point2& d,
                             double permanent) noexcept {
    // ---- Step 1: compute 6 input-difference heads and tails -------------
    double adx, adxtail;
    two_diff(a.x, d.x, adx, adxtail);
    double ady, adytail;
    two_diff(a.y, d.y, ady, adytail);
    double bdx, bdxtail;
    two_diff(b.x, d.x, bdx, bdxtail);
    double bdy, bdytail;
    two_diff(b.y, d.y, bdy, bdytail);
    double cdx, cdxtail;
    two_diff(c.x, d.x, cdx, cdxtail);
    double cdy, cdytail;
    two_diff(c.y, d.y, cdy, cdytail);

    // ---- Step 2: Layer-B exact cofactor expansions ----------------------
    //
    // The three 2x2 cofactors of the incircle 3x3 matrix (expanded along
    // the paraboloid-lift column):
    //
    //   ab = adx*bdy - ady*bdx  (cofactor for third-column element at row c)
    //   bc = bdx*cdy - bdy*cdx  (cofactor for third-column element at row a)
    //   ca = cdx*ady - cdy*adx  (cofactor for third-column element at row b)
    //
    // The signed determinant (expanding along column 3):
    //   det = alift * M_a - blift * M_b + clift * M_c
    // where M_a = bc, M_b = (adx*cdy - ady*cdx) = -ca, M_c = ab. So:
    //   det = alift*bc + blift*ca + clift*ab   (all three terms added, no negation)
    // where alift = adx^2+ady^2, blift = bdx^2+bdy^2, clift = cdx^2+cdy^2.
    //
    // Note: the public incircle_2d wrapper uses the same sign convention.
    // Cofactor assignment (bc→alift, ca→blift, ab→clift) matches
    // Shewchuk's predicates.c incircleadapt structure.

    // ab = adx*bdy - ady*bdx  (4-comp)
    double ab[4];
    {
        double s1, s0, t1, t0;
        two_product(adx, bdy, s1, s0);
        two_product(ady, bdx, t1, t0);
        two_two_diff(s1, s0, t1, t0, ab[3], ab[2], ab[1], ab[0]);
    }

    // bc = bdx*cdy - bdy*cdx  (4-comp)
    double bc[4];
    {
        double s1, s0, t1, t0;
        two_product(bdx, cdy, s1, s0);
        two_product(bdy, cdx, t1, t0);
        two_two_diff(s1, s0, t1, t0, bc[3], bc[2], bc[1], bc[0]);
    }

    // ca = cdx*ady - cdy*adx  (4-comp)
    double ca[4];
    {
        double s1, s0, t1, t0;
        two_product(cdx, ady, s1, s0);
        two_product(cdy, adx, t1, t0);
        two_two_diff(s1, s0, t1, t0, ca[3], ca[2], ca[1], ca[0]);
    }

    // Floating-point scalar squared-lifts (used for Layer-B scaling).
    // These approximate adx^2+ady^2 etc. as a single double. The error
    // from using these approximations is corrected in Layer C.
    const double alift = adx * adx + ady * ady;
    const double blift = bdx * bdx + bdy * bdy;
    const double clift = cdx * cdx + cdy * cdy;

    // Scale each 4-comp cofactor by its scalar squared-lift → up to 8-comp.
    //   adet = alift * bc  (det contribution from row a)
    //   bdet = blift * ca  (det contribution from row b, note sign flip below)
    //   cdet = clift * ab  (det contribution from row c)
    double adet[8];
    int alen = scale_expansion_zeroelim(4, bc, alift, adet);

    double bdet[8];
    int blen = scale_expansion_zeroelim(4, ca, blift, bdet);

    double cdet[8];
    int clen = scale_expansion_zeroelim(4, ab, clift, cdet);

    // Sum three scaled cofactors into the Layer-B running expansion.
    // det = alift*bc + blift*ca + clift*ab (all terms added with positive sign;
    // see the comment above the cofactor definitions for the derivation).
    // No sign flip needed for bdet: the formula uses +blift*ca, not -blift*ca.

    double fin1[kIccMaxExpansion];

    {
        double abdet[kIccMaxExpansion];
        int ablen = expansion_sum_zeroelim(alen, adet, blen, bdet, abdet);
        int finlen = expansion_sum_zeroelim(ablen, abdet, clen, cdet, fin1);

        // ---- Layer-B threshold check ------------------------------------
        double det = estimate(finlen, fin1);
        double errbound = kIccErrBoundB * permanent;
        if ((det >= errbound) || (-det >= errbound)) {
            return det;
        }

        // ---- Layer C: full exact determinant over 2-component entries -------
        //
        // Same repair as orient3d_adapt (see its Step-3 comment): the upstream
        // auracad correction cascade was inexact (rounded scalar pre-products,
        // rounded cofactor-estimate heads); replaced with the full exact
        // determinant over 2-component difference entries.
        //
        //   det = liftA*(Bx*Cy - By*Cx) + liftB*(Cx*Ay - Cy*Ax)
        //       + liftC*(Ax*By - Ay*Bx)
        //
        // with A = a-d, B = b-d, C = c-d, liftX = X.x^2 + X.y^2; identical sign
        // convention to Layer B above. Max lengths: lift <= 16, minor <= 16,
        // product <= 512, total <= 1536.
        const double ex_ax[2] = {adxtail, adx};
        const double ex_ay[2] = {adytail, ady};
        const double ex_bx[2] = {bdxtail, bdx};
        const double ex_by[2] = {bdytail, bdy};
        const double ex_cx[2] = {cdxtail, cdx};
        const double ex_cy[2] = {cdytail, cdy};

        double lift_a[16], lift_b[16], lift_c[16];
        const int lalen = exact_lift2(ex_ax, ex_ay, lift_a);
        const int lblen = exact_lift2(ex_bx, ex_by, lift_b);
        const int lclen = exact_lift2(ex_cx, ex_cy, lift_c);

        double mbc[16], mca[16], mab[16];
        const int mbclen = exact_minor2(ex_bx, ex_cy, ex_by, ex_cx, mbc);
        const int mcalen = exact_minor2(ex_cx, ex_ay, ex_cy, ex_ax, mca);
        const int mablen = exact_minor2(ex_ax, ex_by, ex_ay, ex_bx, mab);

        constexpr int kExactProd = 512;  // 2 * 16 * 16
        double det_a[kExactProd], det_b[kExactProd], det_c[kExactProd];
        double workp[kExactProd];
        const int dalen = expansion_product(lalen, lift_a, mbclen, mbc, det_a, workp);
        const int dblen = expansion_product(lblen, lift_b, mcalen, mca, det_b, workp);
        const int dclen = expansion_product(lclen, lift_c, mablen, mab, det_c, workp);

        double sum_ab[2 * kExactProd];
        const int sablen = expansion_sum_zeroelim(dalen, det_a, dblen, det_b, sum_ab);
        double fin_exact[3 * kExactProd];
        const int exlen = expansion_sum_zeroelim(sablen, sum_ab, dclen, det_c, fin_exact);
        return fin_exact[exlen - 1];
    }
}

// ---- orient2d adaptive cascade -----------------------------------------
//
// orient2d_adapt is invoked when the filtered (layer-A) estimate failed
// to disambiguate the sign. Direct port of Shewchuk's `orient2dadapt`
// (Figure 27 of the 1997 paper / `predicates.c`).
//
// Layer B: compute the determinant exactly using two_product +
// two_two_diff on the 2-component subtraction results -- a 4-component
// expansion whose summed value has the precision of one full multiply
// of double-precision inputs. If its magnitude clears kCcwErrBoundB *
// det_sum, return.
//
// Layer C: capture the rounding tails of (a.x - c.x), (a.y - c.y),
// (b.x - c.x), (b.y - c.y) via two_diff and refine the determinant by
// the four extra cross-products those tails produce. If still
// inconclusive, perform a full expansion of those cross-products
// (eight more two_products, summed via expansion_sum_zeroelim into a
// single non-overlapping expansion) and return the largest-magnitude
// component, whose sign is guaranteed correct (Shewchuk Lemma 5).
//
// This commit does NOT implement Shewchuk's layer D (the rare case
// where layer C is itself inconclusive). The static buffer sizes below
// are sized for the layer-C worst case (16 components -- Shewchuk
// §4.4); a layer-D extension would just enlarge them.
//
// The returned double's SIGN is the correct orientation. Magnitude has
// no operational meaning.
inline double orient2d_adapt(const Point2& a, const Point2& b, const Point2& c,
                             double det_sum) noexcept {
    // ---- Layer B ------------------------------------------------------

    const double acx = a.x - c.x;
    const double bcx = b.x - c.x;
    const double acy = a.y - c.y;
    const double bcy = b.y - c.y;

    double detleft, detlefttail;
    two_product(acx, bcy, detleft, detlefttail);
    double detright, detrighttail;
    two_product(acy, bcx, detright, detrighttail);

    // B[0..3]: low-to-high non-overlapping expansion of
    //   (detleft + detlefttail) - (detright + detrighttail).
    double B[4];
    two_two_diff(detleft, detlefttail, detright, detrighttail, B[3], B[2], B[1], B[0]);

    double det = estimate(4, B);
    double errbound = kCcwErrBoundB * det_sum;
    if ((det >= errbound) || (-det >= errbound)) {
        return det;
    }

    // ---- Layer C ------------------------------------------------------
    //
    // Capture the tails of the four input subtractions via two_diff.
    // Since acx etc. are already the head, recompute via two_diff to
    // pick up just the rounding remainder.
    double acxtail, acytail, bcxtail, bcytail;
    {
        double head;
        two_diff(a.x, c.x, head, acxtail);
        (void)head;
        two_diff(b.x, c.x, head, bcxtail);
        (void)head;
        two_diff(a.y, c.y, head, acytail);
        (void)head;
        two_diff(b.y, c.y, head, bcytail);
        (void)head;
    }

    // Short-circuit: if all four tails are zero, layer B was exact and
    // its sign is the truth.
    if ((acxtail == 0.0) && (acytail == 0.0) && (bcxtail == 0.0) && (bcytail == 0.0)) {
        return det;
    }

    // Refined error bound: now we know how much the next correction can
    // shift the value, so the threshold is tighter and includes a
    // |det|-scaled term.
    errbound = kCcwErrBoundC * det_sum + kResultErrBound * std::fabs(det);
    det += (acx * bcytail + bcy * acxtail) - (acy * bcxtail + bcx * acytail);
    if ((det >= errbound) || (-det >= errbound)) {
        return det;
    }

    // ---- Layer C continuation: full adaptive expansion of the four
    // tail-bearing cross-products. Build each cross-product as a
    // 2-component expansion via two_product, combine the two halves of
    // each side as a 4-component expansion via two_two_diff, then sum
    // everything into a single non-overlapping expansion h. ----------

    // s = acx * bcytail - acy * bcxtail (high precision)
    double s1, s0, t1, t0;
    two_product(acxtail, bcy, s1, s0);
    two_product(acytail, bcx, t1, t0);
    double u_buf[4];
    two_two_diff(s1, s0, t1, t0, u_buf[3], u_buf[2], u_buf[1], u_buf[0]);

    double C1[8];
    int C1len = expansion_sum_zeroelim(4, B, 4, u_buf, C1);

    // v = acx * bcytail - acy * bcxtail
    two_product(acx, bcytail, s1, s0);
    two_product(acy, bcxtail, t1, t0);
    double v_buf[4];
    two_two_diff(s1, s0, t1, t0, v_buf[3], v_buf[2], v_buf[1], v_buf[0]);

    double C2[12];
    int C2len = expansion_sum_zeroelim(C1len, C1, 4, v_buf, C2);

    // w = acxtail * bcytail - acytail * bcxtail
    two_product(acxtail, bcytail, s1, s0);
    two_product(acytail, bcxtail, t1, t0);
    double w_buf[4];
    two_two_diff(s1, s0, t1, t0, w_buf[3], w_buf[2], w_buf[1], w_buf[0]);

    double D[16];
    int Dlen = expansion_sum_zeroelim(C2len, C2, 4, w_buf, D);

    // The largest-magnitude component (last in low-to-high order) has
    // the same sign as the entire expansion -- Shewchuk Lemma 5. Note
    // expansion_sum_zeroelim guarantees Dlen >= 1 even when the true
    // sum is zero (in which case D[0] == 0).
    return D[Dlen - 1];
}

// Are all coordinates of the three input points finite (not NaN, not
// infinity)? orient2d returns INDETERMINATE on non-finite input rather
// than feeding NaN/inf through the adaptive cascade (where they would
// propagate silently and produce a nonsense sign).
inline bool all_finite(const Point2& a, const Point2& b, const Point2& c) noexcept {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(b.x) && std::isfinite(b.y) &&
           std::isfinite(c.x) && std::isfinite(c.y);
}

}  // namespace

Orientation orient2d(const Point2& a, const Point2& b, const Point2& c) noexcept {
    // Non-finite (NaN / +-inf) coordinates are the SOLE remaining case
    // that returns INDETERMINATE from orient2d as of Phase 2.5a. Every
    // finite input case is resolved to LEFT / RIGHT / COLLINEAR by the
    // adaptive cascade below.
    if (!all_finite(a, b, c)) {
        return Orientation::INDETERMINATE;
    }

    // Pivot on c, exactly like Shewchuk's orient2d AND orient2d_adapt below.
    // (b-a)x(c-a) == (a-c)x(b-c) in exact arithmetic, so the sign convention
    // is unchanged — but the PIVOT MUST MATCH the adaptive cascade: det_sum
    // feeds orient2d_adapt's Layer-B/C error bounds, which are derived for
    // the pivot-c products. The upstream auracad wrapper pivoted on `a` while
    // the cascade pivots on `c`; with mismatched product magnitudes the
    // Layer-B bound under-estimated the error and certified WRONG SIGNS
    // (fuzzing: cyclic-rotation inconsistencies on ~0.5% of near-degenerate
    // inputs, 2026-06-11; see research/auracad/predicates-state.md).
    const double acx = a.x - c.x;
    const double bcx = b.x - c.x;
    const double acy = a.y - c.y;
    const double bcy = b.y - c.y;

    const double left_term = acx * bcy;
    const double right_term = acy * bcx;
    const double det = left_term - right_term;

    // ---- Layer A (filter): Shewchuk's static error bound ccwerrboundA.
    // Reference: Shewchuk 1997 §4.4. The bound scales with the magnitudes
    // of the post-multiply intermediates; if |det| exceeds it, the sign is
    // provably correct.
    const double abs_left = std::fabs(left_term);
    const double abs_right = std::fabs(right_term);
    const double det_sum = abs_left + abs_right;
    const double err_bound_a = det_sum * kCcwErrBoundA;

    if (std::fabs(det) > err_bound_a) {
        return det > 0.0 ? Orientation::LEFT : Orientation::RIGHT;
    }

    // ---- Layers B + C (adaptive): when the filter cannot decide.
    // Returns a double whose SIGN is the correct orientation. An exact
    // zero return means the three points are exactly collinear in
    // infinite precision.
    const double adapt = orient2d_adapt(a, b, c, det_sum);
    if (adapt > 0.0) return Orientation::LEFT;
    if (adapt < 0.0) return Orientation::RIGHT;
    return Orientation::COLLINEAR;
}

Orientation orient3d(const Point3& a, const Point3& b, const Point3& c, const Point3& d) noexcept {
    // Non-finite (NaN / +-inf) coordinates are the SOLE remaining case
    // that returns INDETERMINATE from orient3d as of Phase 2.5b. Every
    // finite input resolves to LEFT / RIGHT / COLLINEAR via the adaptive
    // cascade below.
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) || !std::isfinite(b.x) ||
        !std::isfinite(b.y) || !std::isfinite(b.z) || !std::isfinite(c.x) || !std::isfinite(c.y) ||
        !std::isfinite(c.z) || !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) {
        return Orientation::INDETERMINATE;
    }

    // Signed volume of tetrahedron (a, b, c, d) is (1/6) * det(M) where M
    // is the 3x3 matrix of (b-a, c-a, d-a) row vectors (a is the pivot).
    // Variable naming follows Shewchuk predicates.c orient3d / orient3dadapt
    // with the mapping adx=b.x-a.x, bdx=c.x-a.x, cdx=d.x-a.x.
    const double adx = b.x - a.x;
    const double ady = b.y - a.y;
    const double adz = b.z - a.z;

    const double bdx = c.x - a.x;
    const double bdy = c.y - a.y;
    const double bdz = c.z - a.z;

    const double cdx = d.x - a.x;
    const double cdy = d.y - a.y;
    const double cdz = d.z - a.z;

    // ---- Layer A filter (Shewchuk §4.5, kO3dErrBoundA) ------------------
    //
    // Cofactor expansion along the first column (adx):
    //   adx*(bdy*cdz - bdz*cdy) - ady*(bdx*cdz - bdz*cdx) + adz*(bdx*cdy - bdy*cdx)
    //
    // 2x2 cofactor products (used both for det and permanent).
    const double p00 = bdy * cdz;
    const double p01 = bdz * cdy;
    const double p10 = bdx * cdz;
    const double p11 = bdz * cdx;
    const double p20 = bdx * cdy;
    const double p21 = bdy * cdx;

    const double minor_a = p00 - p01;
    const double minor_b = p10 - p11;
    const double minor_c = p20 - p21;

    const double det = adx * minor_a - ady * minor_b + adz * minor_c;

    // Shewchuk permanent (sum of absolute product magnitudes):
    //   permanent = |adx|*(|p00|+|p01|) + |ady|*(|p10|+|p11|) + |adz|*(|p20|+|p21|)
    const double bm_a = std::fabs(p00) + std::fabs(p01);
    const double bm_b = std::fabs(p10) + std::fabs(p11);
    const double bm_c = std::fabs(p20) + std::fabs(p21);
    const double permanent = std::fabs(adx) * bm_a + std::fabs(ady) * bm_b + std::fabs(adz) * bm_c;
    const double err_bound_a = kO3dErrBoundA * permanent;

    if (std::fabs(det) > err_bound_a) {
        return det > 0.0 ? Orientation::LEFT : Orientation::RIGHT;
    }

    // ---- Layers B + C (adaptive): when the filter cannot decide ----------
    //
    // orient3d_adapt computes the determinant to sufficient precision to
    // resolve the sign. Returns a double whose SIGN is the correct
    // orientation; zero means exactly coplanar.
    const double adapt = orient3d_adapt(a, b, c, d, permanent);
    if (adapt > 0.0) return Orientation::LEFT;
    if (adapt < 0.0) return Orientation::RIGHT;
    return Orientation::COLLINEAR;
}

bool on_segment_2d(const Point2& a, const Point2& b, const Point2& p) noexcept {
    using ingeneer::geom::numeric::LENGTH_EPSILON;

    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double apx = p.x - a.x;
    const double apy = p.y - a.y;

    const double seg_len_sq = abx * abx + aby * aby;
    const double dot_ap_ab = apx * abx + apy * aby;
    const double cross_ap_ab = apx * aby - apy * abx;

    // Degenerate segment (a == b within LENGTH_EPSILON): the segment is a
    // single point; p is on it iff p coincides with a within LENGTH_EPSILON.
    if (seg_len_sq <= LENGTH_EPSILON * LENGTH_EPSILON) {
        const double dx = p.x - a.x;
        const double dy = p.y - a.y;
        return (dx * dx + dy * dy) <= LENGTH_EPSILON * LENGTH_EPSILON;
    }

    // Orthogonal distance from p to the infinite line through a-b is
    // |cross| / |ab|. Square both sides to avoid the sqrt.
    const double dist_sq_times_seg_len_sq = cross_ap_ab * cross_ap_ab;
    const double tol_sq_times_seg_len_sq = LENGTH_EPSILON * LENGTH_EPSILON * seg_len_sq;
    if (dist_sq_times_seg_len_sq > tol_sq_times_seg_len_sq) {
        return false;
    }

    // Parametric position t = (ap . ab) / (ab . ab). Avoid the divide:
    // require 0 * seg_len_sq <= dot_ap_ab <= 1 * seg_len_sq, with a
    // LENGTH_EPSILON-scaled slop on each end so that points at the very
    // tip of the segment count as inside.
    // Squared-space parametric clamp — avoids a sqrt to keep this hot
    // tessellation/intersection inner loop sqrt-free (consistent with the
    // orthogonal-distance check above). The original test
    //     dot_ap_ab in [-LENGTH_EPSILON*|ab|, |ab|^2 + LENGTH_EPSILON*|ab|]
    // is squared on each side: when dot_ap_ab < 0, require
    //     dot_ap_ab^2 <= LENGTH_EPSILON^2 * seg_len_sq
    // and when dot_ap_ab > seg_len_sq, the same bound on (dot_ap_ab - seg_len_sq).
    const double slack_sq = LENGTH_EPSILON * LENGTH_EPSILON * seg_len_sq;
    if (dot_ap_ab < 0.0 && dot_ap_ab * dot_ap_ab > slack_sq) return false;
    const double excess = dot_ap_ab - seg_len_sq;
    if (excess > 0.0 && excess * excess > slack_sq) return false;
    return true;
}

bool bbox_overlap_2d(const AABB2& a, const AABB2& b) noexcept {
    // Inclusive endpoints. Strict comparison; callers expand boxes by an
    // epsilon themselves if they want loose overlap. NaN inputs propagate
    // to false (every comparison with NaN returns false), which matches
    // the documented contract -- a NaN-cornered box is degenerate input
    // and "overlaps nothing" is the safe answer.
    if (a.xmax < b.xmin) return false;
    if (b.xmax < a.xmin) return false;
    if (a.ymax < b.ymin) return false;
    if (b.ymax < a.ymin) return false;
    return true;
}

Orientation incircle_2d(const Point2& a, const Point2& b, const Point2& c,
                        const Point2& d) noexcept {
    // Non-finite (NaN / +-inf) coordinates are the SOLE remaining case
    // that returns INDETERMINATE from incircle_2d as of Phase 2.5c. Every
    // finite input resolves to LEFT / RIGHT / COLLINEAR via the adaptive
    // cascade below.
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        !std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(d.x) || !std::isfinite(d.y)) {
        return Orientation::INDETERMINATE;
    }

    // Standard Shewchuk in-circle determinant:
    //
    //     | a.x - d.x   a.y - d.y   (a.x - d.x)^2 + (a.y - d.y)^2 |
    //     | b.x - d.x   b.y - d.y   (b.x - d.x)^2 + (b.y - d.y)^2 |
    //     | c.x - d.x   c.y - d.y   (c.x - d.x)^2 + (c.y - d.y)^2 |
    //
    // Positive (LEFT) when d is strictly inside the circumcircle of CCW
    // (a, b, c); zero (COLLINEAR) when on the circle; negative (RIGHT) when
    // outside.

    const double adx = a.x - d.x;
    const double ady = a.y - d.y;
    const double bdx = b.x - d.x;
    const double bdy = b.y - d.y;
    const double cdx = c.x - d.x;
    const double cdy = c.y - d.y;

    const double ad_sq = adx * adx + ady * ady;
    const double bd_sq = bdx * bdx + bdy * bdy;
    const double cd_sq = cdx * cdx + cdy * cdy;

    // Cofactor expansion along the third column. Naming conventions follow
    // Shewchuk's predicates.c incircleadapt; the cofactors are:
    //   minor_a = bdx*cdy - bdy*cdx = bc  (paired with ad_sq = alift)
    //   minor_b = cdx*ady - cdy*adx = ca  (paired with bd_sq = blift, + sign)
    //   minor_c = adx*bdy - ady*bdx = ab  (paired with cd_sq = clift)
    //
    // The signed determinant: det = ad_sq*minor_a + bd_sq*minor_b + cd_sq*minor_c
    // (using the sign convention where minor_b = ca = cdx*ady - cdy*adx,
    // so the -blift*(adx*cdy-ady*cdx) term = +blift*ca).
    const double minor_a = bdx * cdy - bdy * cdx;  // bc
    const double minor_b = cdx * ady - cdy * adx;  // ca
    const double minor_c = adx * bdy - ady * bdx;  // ab

    const double det = ad_sq * minor_a + bd_sq * minor_b + cd_sq * minor_c;

    // ---- Layer A (filter): Shewchuk's static error bound iccerrboundA ---
    //
    // Permanent (sum of absolute product magnitudes), per Shewchuk's incircle():
    //   permanent = (|bdx*cdy| + |bdy*cdx|)*alift
    //             + (|cdx*ady| + |cdy*adx|)*blift
    //             + (|adx*bdy| + |ady*bdx|)*clift
    // The products must be taken BEFORE the cancelling subtraction: the rounding
    // error of each minor scales with the product magnitudes, not with the
    // (possibly fully cancelled) difference. Using |minor| here made the bound
    // arbitrarily too small on near-cocircular and large-coordinate input, so the
    // filter certified wrong float signs without reaching the exact layers — and
    // the same too-small permanent poisoned incircle_adapt's B/C thresholds
    // (Phase 6.5 fuzz finding; regression vectors in test_predicates.cpp). The
    // lifts ad_sq/bd_sq/cd_sq are sums of squares, hence already nonnegative.
    const double permanent = (std::fabs(bdx * cdy) + std::fabs(bdy * cdx)) * ad_sq +
                             (std::fabs(cdx * ady) + std::fabs(cdy * adx)) * bd_sq +
                             (std::fabs(adx * bdy) + std::fabs(ady * bdx)) * cd_sq;
    const double err_bound_a = kIccErrBoundA * permanent;

    if (std::fabs(det) > err_bound_a) {
        return det > 0.0 ? Orientation::LEFT : Orientation::RIGHT;
    }

    // ---- Layers B + C (adaptive): when the filter cannot decide ----------
    //
    // incircle_adapt computes the determinant to sufficient precision to
    // resolve the sign. Returns a double whose SIGN is the correct answer;
    // zero means the four points are exactly cocircular.
    const double adapt = incircle_adapt(a, b, c, d, permanent);
    if (adapt > 0.0) return Orientation::LEFT;
    if (adapt < 0.0) return Orientation::RIGHT;
    return Orientation::COLLINEAR;
}

}  // namespace ingeneer::geom::predicates
