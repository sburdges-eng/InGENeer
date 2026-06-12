// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/geom/numeric_policy.h
//
// Ported from auracad geom (branch agentic/predicates-adaptive-3d-2026-04-24)
// per ADR-0024 ("promote verbatim"). Only the namespace and include guards
// were changed; every constant value and design note is preserved exactly.
//
// Project numeric policy for the InGENeer geometry kernel.
//
// This header is the single source of truth for the project's stance on
// floating-point arithmetic, geometric tolerances, and predicate robustness.
// It is consumed by the kernel, by callers in higher layers, and by tests
// that need to make tolerance choices visible at compile time.
//
// Invariants this header codifies (inherited from the auracad rule docs):
//
//   * Determinism: identical inputs MUST produce identical outputs across
//     runs and across supported platforms. No reliance on ordering of a
//     hash map, on uninitialized memory, on time-dependent state, etc.
//
//   * -ffast-math, -Ofast, -ffinite-math-only, -freciprocal-math are
//     forbidden. IEEE 754 binary64 semantics (including subnormals, signed
//     zero, +/-inf, NaN propagation) are load-bearing for the predicates
//     declared below.
//
//   * Sanitizer coverage is required for every kernel module that depends
//     on these tolerances.
//
//   * Robust geometric predicates (orientation, in-circle, in-sphere)
//     come BEFORE any tolerance-based fallback. Tolerance is the last
//     resort, not the first; the constants here are an upper bound on
//     what the kernel is willing to forgive once exact predicates have
//     already classified a configuration as degenerate.
//
// Header design notes:
//
//   * Pure header. No implementation. No third-party dependency. Only the
//     standard library (<limits>, <cstddef>) is included so the file is
//     trivially includable by any translation unit (FFI shims included).
//
//   * Constants are `inline constexpr double` so they have a single ODR-safe
//     definition across all translation units (C++17+ inline-variable rule)
//     and are usable in `static_assert` and `constexpr` contexts.
//
//   * The `NumericPolicy` class exists only as a runtime introspection
//     surface (e.g. for diagnostic dumps, logging, or Python bindings).
//     It must NOT be instantiated; copy/move are deleted.
//
//   * The IEEE 754 / IEC 559 static_asserts are the load-bearing guard
//     against accidental builds with -ffast-math (which on some toolchains
//     defines __FAST_MATH__ and may flip is_iec559 to false), against exotic
//     extended-precision `double`, and against any future port to a
//     platform whose `double` is not binary64.
//
// If you change a constant in this file you MUST:
//   1. Note the change in the governance / architecture docs.
//   2. Re-run the regression suite (every test that compares geometry).
//   3. Justify the change in the commit message; tolerances are part of
//      the public ABI for the kernel's correctness contract.

#ifndef INGENEER_GEOM_NUMERIC_POLICY_H
#define INGENEER_GEOM_NUMERIC_POLICY_H

#include <cstddef>
#include <limits>

namespace ingeneer::geom::numeric {

// ---- Compile-time IEEE 754 / IEC 559 guards ------------------------------
//
// The predicates and tolerances below assume `double` is exactly IEEE 754
// binary64. If a future port lands on a platform where this is not true
// (or a build slips through with -ffast-math toggling is_iec559 to false on
// some compilers), the kernel must NOT silently degrade -- it must fail to
// compile.

static_assert(sizeof(double) == 8,
              "ingeneer::geom::numeric requires IEEE 754 binary64 doubles "
              "(sizeof(double) == 8). The kernel's robust predicates and "
              "tolerance constants are calibrated for binary64 only.");

static_assert(std::numeric_limits<double>::is_iec559,
              "ingeneer::geom::numeric requires IEC 559 / IEEE 754 doubles. "
              "If this assertion fires, you are likely building with "
              "-ffast-math or on an exotic FP target -- both forbidden by "
              "the project numeric policy (C-4.2).");

// ---- Tolerance constants -------------------------------------------------
//
// The constants are intentionally tiered: callers should pick the coarsest
// tolerance that is still correct for the question being asked. Using
// EPSILON for a length comparison, for example, is wrong -- length math
// accumulates rounding error proportional to operand magnitude, so the
// length tolerance is deliberately coarser than the raw equality epsilon.

// Geometric equality default. The tightest tolerance the kernel exposes;
// reserved for "are these two normalized scalars bit-equivalent up to one
// or two ULPs of double?" comparisons inside well-conditioned predicates.
inline constexpr double EPSILON = 1e-12;

// Length comparison default. Coarser than EPSILON because length math (sums
// of squares, square roots) accumulates error, and most user-facing CAD
// geometry is in millimeters where 1e-9 is sub-nanometer -- well below any
// real manufacturing tolerance.
inline constexpr double LENGTH_EPSILON = 1e-9;

// Angle equality default, in DEGREES. 1e-6 deg ~= 1.7e-8 rad, which keeps
// surface-normal comparisons stable across reasonable transforms without
// false-equating angles that the user actually distinguishes.
inline constexpr double ANGLE_EPSILON_DEG = 1e-6;

// Filtered-predicate threshold for orientation / incircle / insphere
// determinants. Below this the cheap floating-point predicate is treated
// as inconclusive and the caller MUST fall back to an exact (interval or
// rational) re-evaluation. This is NOT a "geometric tolerance"; it is a
// numerical threshold on the determinant magnitude, which is why it is
// much smaller than EPSILON.
inline constexpr double DETERMINANT_EPSILON = 1e-15;

// ---- Robust-predicates contract -----------------------------------------
//
// Modules that depend on robust (exact) geometric predicates -- as opposed
// to filtered or pure floating-point predicates -- can `static_assert`
// against this flag to make the dependency visible at compile time. The
// flag is `true` because the kernel's correctness contract requires that
// any predicate which can return a wrong sign on any input MUST be backed
// by an exact fallback.

inline constexpr bool kRobustPredicatesRequired = true;

// ---- Runtime introspection surface --------------------------------------
//
// `NumericPolicy` exposes the constants as static member functions for
// callers that need to read them at runtime (Python bindings, diagnostic
// dumps, headless CLI status output). It is final and non-instantiable;
// the constants live as the `inline constexpr` values above and the class
// is a pure facade.

class NumericPolicy final {
public:
    NumericPolicy() = delete;
    ~NumericPolicy() = delete;
    NumericPolicy(const NumericPolicy&) = delete;
    NumericPolicy(NumericPolicy&&) = delete;
    NumericPolicy& operator=(const NumericPolicy&) = delete;
    NumericPolicy& operator=(NumericPolicy&&) = delete;

    static constexpr double epsilon() noexcept { return EPSILON; }
    static constexpr double length_epsilon() noexcept { return LENGTH_EPSILON; }
    static constexpr double angle_epsilon_deg() noexcept { return ANGLE_EPSILON_DEG; }
    static constexpr double determinant_epsilon() noexcept { return DETERMINANT_EPSILON; }
    static constexpr bool robust_predicates_required() noexcept {
        return kRobustPredicatesRequired;
    }
};

}  // namespace ingeneer::geom::numeric

#endif  // INGENEER_GEOM_NUMERIC_POLICY_H
