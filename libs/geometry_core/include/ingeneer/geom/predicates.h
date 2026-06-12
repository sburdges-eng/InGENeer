// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/geom/predicates.h
//
// Robust geometric predicates: filtered double + adaptive multi-precision.
//
// Ported from auracad geom (branch agentic/predicates-adaptive-3d-2026-04-24)
// per ADR-0024; Shewchuk robust predicates (public domain,
// https://www.cs.cmu.edu/~quake/robust.html).
//
// Design constraints (enforced by this header / its tests):
//
//   * Self-contained: only <cstdint>, stdlib, and ingeneer/geom/numeric_policy.h.
//     No OCCT, no other engine headers. This keeps the predicates linkable
//     into FFI shims and into TUs that must not see CAD-host types.
//
//   * Pure value types. Point2/Point3/AABB2 are aggregates: no constructors,
//     no invariants beyond IEEE-754 binary64 fields. Callers may freely
//     construct, copy, and compare them.
//
//   * Every public function is `noexcept` and allocation-free. The
//     implementation uses static-storage arithmetic only; predicates are
//     called from inner loops in tessellation, intersection, and indexing
//     and must not allocate.
//
//   * Validity domain (same caveat as Shewchuk's reference predicates.c):
//     exactness is guaranteed only in the ABSENCE of overflow and underflow.
//     If coordinate differences or their products underflow to subnormals
//     (e.g. a coordinate perturbed within 1 ulp of zero), discarded
//     underflowed bits may change the sign of a nearly-degenerate result.
//     Survey/CAD coordinates (millimeter scale and above, binary64) sit far
//     inside the safe domain. The FP environment must be round-to-nearest
//     with FP contraction OFF (the build passes -ffp-contract=off and the
//     implementation TU carries #pragma STDC FP_CONTRACT OFF — FMA
//     contraction breaks two_product error terms and the Layer-A filters).
//
//   * Determinism: identical inputs produce identical outputs across
//     supported platforms (Linux x86_64 / arm64, macOS arm64). The
//     filtered-double error bound is computed without `std::fma`, without
//     SIMD intrinsics, and without compiler-flag-dependent reassociation.
//
//   * All three adaptive predicates (orient2d, orient3d, incircle_2d)
//     return INDETERMINATE only on non-finite input. The Shewchuk-style
//     adaptive multi-precision expansion eliminates INDETERMINATE on
//     every finite-input case.

#ifndef INGENEER_GEOM_PREDICATES_H
#define INGENEER_GEOM_PREDICATES_H

#include <cstdint>

#include "ingeneer/geom/numeric_policy.h"

// If anyone flips kRobustPredicatesRequired to false, this module's
// correctness contract no longer holds. Fail compilation rather than
// silently degrade.
static_assert(ingeneer::geom::numeric::kRobustPredicatesRequired,
              "ingeneer/geom/predicates.h requires "
              "ingeneer::geom::numeric::kRobustPredicatesRequired == true. "
              "Filtered-double predicates are only sound under the "
              "robust-predicates contract; see numeric_policy.h.");

namespace ingeneer::geom::predicates {

// ---- Value types --------------------------------------------------------

struct Point2 {
    double x;
    double y;
};

struct Point3 {
    double x;
    double y;
    double z;
};

// Axis-aligned bounding box in 2D. Inclusive endpoints; callers that need
// epsilon-padded overlap tests must expand the box themselves before
// calling bbox_overlap_2d.
struct AABB2 {
    double xmin;
    double ymin;
    double xmax;
    double ymax;
};

// ---- Predicate result encoding ------------------------------------------
//
// One enum is reused across orient2d / orient3d / incircle_2d to keep the
// caller-side switch shape uniform. The numeric encoding is deliberate so
// that callers can write `static_cast<int>(o) > 0` for the "positive sign"
// branch (LEFT for orientation, INSIDE for incircle) when all they care
// about is the sign.
//
// INDETERMINATE means: at least one coordinate of the input is non-finite
// (NaN or +-inf). All adaptive predicates (orient2d, orient3d, incircle_2d)
// resolve every finite-input configuration to a concrete LEFT / RIGHT /
// COLLINEAR; INDETERMINATE is not reachable on finite inputs. Treating
// INDETERMINATE as COLLINEAR is a correctness bug.

enum class Orientation : std::int8_t {
    LEFT = 1,            // c is to the left of a->b (CCW); INSIDE for incircle
    COLLINEAR = 0,       // exactly collinear / coplanar / on circle
    RIGHT = -1,          // c is to the right (CW); OUTSIDE for incircle
    INDETERMINATE = -2,  // non-finite input only; never returned for finite input
};

// ---- Predicates ---------------------------------------------------------

// Sign of the 2x2 determinant
//
//     | b.x - a.x   c.x - a.x |
//     | b.y - a.y   c.y - a.y |
//
// Returns LEFT for positive determinant (c is on the left of the directed
// segment a->b, i.e. (a, b, c) is counter-clockwise), RIGHT for negative,
// and COLLINEAR when the determinant is exactly zero (the three points lie
// on a line in infinite precision). The SOLE case that returns
// INDETERMINATE is non-finite input: any of a/b/c containing NaN or +-inf.
// Every finite-input call resolves to a definite LEFT / RIGHT / COLLINEAR
// via the adaptive cascade in src/predicates.cpp.
Orientation orient2d(const Point2& a, const Point2& b, const Point2& c) noexcept;

// Sign of the signed volume (times 6) of tetrahedron (a, b, c, d). LEFT
// for positive volume (right-handed tetrahedron when (a, b, c) is CCW
// viewed from d), RIGHT for negative, COLLINEAR when the four points are
// exactly coplanar in infinite precision. The SOLE case that returns
// INDETERMINATE is non-finite input: any of a/b/c/d containing NaN or
// +-inf. Every finite-input call resolves to a definite LEFT / RIGHT /
// COLLINEAR via the adaptive cascade in src/predicates.cpp.
Orientation orient3d(const Point3& a, const Point3& b, const Point3& c, const Point3& d) noexcept;

// Returns true iff p lies on the closed segment a-b within LENGTH_EPSILON
// orthogonal distance and parametric position t in [0, 1]. For zero-length
// segments (a == b within LENGTH_EPSILON) returns true iff p coincides
// with a within LENGTH_EPSILON.
bool on_segment_2d(const Point2& a, const Point2& b, const Point2& p) noexcept;

// Inclusive AABB overlap. Returns true iff the boxes share at least one
// point (including touching edges or corners). Callers that need strict
// (open) overlap or epsilon-padded overlap must adjust the inputs.
bool bbox_overlap_2d(const AABB2& a, const AABB2& b) noexcept;

// In-circle predicate: given triangle (a, b, c) in CCW order, is point d
// strictly inside, exactly on, or strictly outside the unique circumcircle?
// Returns LEFT for INSIDE, COLLINEAR for ON (exactly cocircular), and RIGHT
// for OUTSIDE. Callers that pass a CW triangle will see the result with
// flipped sign -- this matches Shewchuk's convention. The SOLE case that
// returns INDETERMINATE is non-finite input: any of a/b/c/d containing NaN
// or +-inf. Every finite-input call resolves to a definite LEFT / RIGHT /
// COLLINEAR via the adaptive cascade in src/predicates.cpp.
Orientation incircle_2d(const Point2& a, const Point2& b, const Point2& c,
                        const Point2& d) noexcept;

}  // namespace ingeneer::geom::predicates

#endif  // INGENEER_GEOM_PREDICATES_H
