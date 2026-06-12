// test_predicates.cpp — regression + degeneracy suite for ingeneer/geom/predicates.h
// (Phase 5 exit gate).
//
// Ported from auracad tests/harness/predicates_test.cpp (branch
// agentic/predicates-adaptive-3d-2026-04-24) per ADR-0024, rewritten on the repo's
// check.hpp harness and extended with:
//   - exact-degenerate fixtures (collinear / coplanar / cocircular) built from
//     coordinates exactly representable in binary64 (powers of two, small integers);
//   - sign-correctness on near-degenerate ±1-ulp perturbations via std::nextafter;
//   - symmetry/antisymmetry properties (swapping two points flips the sign).

#include "ingeneer/geom/predicates.h"

#include <cmath>
#include <limits>

#include "check.hpp"
#include "ingeneer/geom/numeric_policy.h"

using ingeneer::geom::numeric::LENGTH_EPSILON;
using ingeneer::geom::predicates::AABB2;
using ingeneer::geom::predicates::bbox_overlap_2d;
using ingeneer::geom::predicates::incircle_2d;
using ingeneer::geom::predicates::on_segment_2d;
using ingeneer::geom::predicates::orient2d;
using ingeneer::geom::predicates::orient3d;
using ingeneer::geom::predicates::Orientation;
using ingeneer::geom::predicates::Point2;
using ingeneer::geom::predicates::Point3;

namespace {

// Sign flip helper: LEFT <-> RIGHT, COLLINEAR fixed. INDETERMINATE never expected here.
Orientation flipped(Orientation o) {
    if (o == Orientation::LEFT) return Orientation::RIGHT;
    if (o == Orientation::RIGHT) return Orientation::LEFT;
    return o;
}

// ---- orient2d -----------------------------------------------------------

void test_orient2d_basic() {
    // (0,0) -> (1,0); c at (0,1) is on the LEFT (CCW).
    CHECK(orient2d({0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}) == Orientation::LEFT);
    CHECK(orient2d({0.0, 0.0}, {1.0, 0.0}, {0.0, -1.0}) == Orientation::RIGHT);
    CHECK(orient2d({0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}) == Orientation::COLLINEAR);
    CHECK(orient2d({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}) == Orientation::COLLINEAR);
}

void test_orient2d_known_signs() {
    struct Case {
        Point2 a;
        Point2 b;
        Point2 c;
        Orientation expected;
    };
    // Hand-computed auracad regression batch: each determinant magnitude is well above
    // the filter bound, so the filter path decides every case.
    const Case cases[] = {
        {{0, 0}, {1, 0}, {1, 1}, Orientation::LEFT},
        {{0, 0}, {1, 0}, {1, -1}, Orientation::RIGHT},
        {{0, 0}, {0, 1}, {-1, 1}, Orientation::LEFT},
        {{0, 0}, {0, 1}, {1, 1}, Orientation::RIGHT},
        {{-5, -5}, {5, 5}, {-5, 5}, Orientation::LEFT},
        {{-5, -5}, {5, 5}, {5, -5}, Orientation::RIGHT},
        {{2, 3}, {7, 11}, {1, -4}, Orientation::RIGHT},
        {{2, 3}, {7, 11}, {0, 100}, Orientation::LEFT},
        {{0, 0}, {3, 4}, {6, 8}, Orientation::COLLINEAR},
        {{1, 1}, {4, 5}, {7, 9}, Orientation::COLLINEAR},
    };
    for (const auto& tc : cases) {
        CHECK(orient2d(tc.a, tc.b, tc.c) == tc.expected);
    }
}

void test_orient2d_degenerate_filter() {
    // Massive coordinates with a sub-ULP perturbation: at scale 1e9 the ULP is ~1.19e-7,
    // so adding 1e-9 rounds away and the three points are EXACTLY collinear in stored
    // representation. The adaptive cascade resolves to COLLINEAR.
    const double huge = 1.0e9;
    const double tiny = 1.0e-9;
    CHECK(orient2d({0.0, 0.0}, {huge, huge}, {huge + tiny, huge + tiny}) == Orientation::COLLINEAR);
}

void test_orient2d_adaptive_resolves_filter_failures() {
    // auracad's hand-constructed filter-defeating vectors: |det| lands inside the static
    // error envelope, the adaptive cascade must recover the true sign.
    //   huge = 2^30, delta = 2^-22 = ULP(huge); huge + delta is exactly representable.
    constexpr double huge = static_cast<double>(1ULL << 30);
    constexpr double delta = 1.0 / static_cast<double>(1ULL << 22);
    const Point2 a{0.0, 0.0};
    const Point2 b{huge, huge};

    // det = -huge*delta = -256 (inside the ~768 filter envelope) -> RIGHT.
    CHECK(orient2d(a, b, {huge + delta, huge}) == Orientation::RIGHT);
    // det = +huge*delta = +256 -> LEFT.
    CHECK(orient2d(a, b, {huge, huge + delta}) == Orientation::LEFT);
    // det = +huge*delta = +256 -> LEFT.
    CHECK(orient2d(a, b, {huge - delta, huge}) == Orientation::LEFT);
    // Exactly collinear at scale (powers of two; exact arithmetic) -> COLLINEAR.
    CHECK(orient2d(a, b, {2.0 * huge, 2.0 * huge}) == Orientation::COLLINEAR);
    // det = -delta^2 = -2^-44, far below the filter envelope -> RIGHT.
    CHECK(orient2d(a, {huge + delta, huge}, {huge, huge - delta}) == Orientation::RIGHT);
}

void test_orient2d_exactly_collinear_fixtures() {
    // Coordinates exactly representable in binary64 (powers of two, small integers);
    // differences and 2x2 products are exact, so COLLINEAR is the only correct answer.
    CHECK(orient2d({1.0, 2.0}, {2.0, 4.0}, {4.0, 8.0}) == Orientation::COLLINEAR);
    CHECK(orient2d({-8.0, -8.0}, {0.0, 0.0}, {1024.0, 1024.0}) == Orientation::COLLINEAR);
    CHECK(orient2d({0.5, 0.25}, {1.0, 0.5}, {2.0, 1.0}) == Orientation::COLLINEAR);
    // Horizontal / vertical exact lines.
    CHECK(orient2d({-3.0, 7.0}, {0.0, 7.0}, {123.0, 7.0}) == Orientation::COLLINEAR);
    CHECK(orient2d({7.0, -3.0}, {7.0, 0.0}, {7.0, 123.0}) == Orientation::COLLINEAR);
    // Coincident points are trivially collinear.
    CHECK(orient2d({3.0, 4.0}, {3.0, 4.0}, {5.0, 6.0}) == Orientation::COLLINEAR);
    CHECK(orient2d({3.0, 4.0}, {3.0, 4.0}, {3.0, 4.0}) == Orientation::COLLINEAR);
}

void test_orient2d_ulp_perturbations() {
    // Collinear +-1 ulp: nudge c off the line y=x by exactly one ulp in y. The exact
    // determinant sign must follow the nudge direction, consistently, at several scales.
    const double scales[] = {1.0, 3.0, 1.0e-7, 1.0e7, 1.0e15};
    for (const double s : scales) {
        const Point2 a{0.0, 0.0};
        const Point2 b{s, s};
        const Point2 c{2.0 * s, 2.0 * s};
        CHECK(orient2d(a, b, c) == Orientation::COLLINEAR);
        const Point2 c_up{c.x, std::nextafter(c.y, std::numeric_limits<double>::infinity())};
        const Point2 c_dn{c.x, std::nextafter(c.y, -std::numeric_limits<double>::infinity())};
        CHECK(orient2d(a, b, c_up) == Orientation::LEFT);
        CHECK(orient2d(a, b, c_dn) == Orientation::RIGHT);
    }
}

void test_orient2d_antisymmetry() {
    // Swapping any two arguments flips the sign; cyclic rotation preserves it.
    const Point2 pts[] = {{0.0, 0.0},     {1.0, 0.0},           {0.3, 0.7},   {-2.0, 5.0},
                          {1.0e8, 1.0e8}, {1.0e8 + 1.0, 1.0e8}, {0.25, 0.125}};
    for (const auto& a : pts) {
        for (const auto& b : pts) {
            for (const auto& c : pts) {
                const Orientation o = orient2d(a, b, c);
                CHECK(o != Orientation::INDETERMINATE);
                CHECK(orient2d(b, a, c) == flipped(o));
                CHECK(orient2d(a, c, b) == flipped(o));
                CHECK(orient2d(c, b, a) == flipped(o));
                CHECK(orient2d(b, c, a) == o);
                CHECK(orient2d(c, a, b) == o);
            }
        }
    }
}

void test_orient2d_nan_returns_indeterminate() {
    // Non-finite input is the SOLE case where orient2d returns INDETERMINATE.
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    const Point2 a{0.0, 0.0};
    const Point2 b{1.0, 0.0};
    CHECK(orient2d({nan, 0.0}, b, {0.0, 1.0}) == Orientation::INDETERMINATE);
    CHECK(orient2d(a, {nan, 0.0}, {0.0, 1.0}) == Orientation::INDETERMINATE);
    CHECK(orient2d(a, b, {0.0, nan}) == Orientation::INDETERMINATE);
    CHECK(orient2d({inf, 0.0}, b, {0.0, 1.0}) == Orientation::INDETERMINATE);
    CHECK(orient2d(a, b, {0.0, -inf}) == Orientation::INDETERMINATE);
}

// ---- orient3d -----------------------------------------------------------

void test_orient3d_basic() {
    const Point3 a{0, 0, 0};
    const Point3 b{1, 0, 0};
    const Point3 c{0, 1, 0};
    CHECK(orient3d(a, b, c, {0, 0, 1}) == Orientation::LEFT);
    CHECK(orient3d(a, b, c, {0, 0, -1}) == Orientation::RIGHT);
    CHECK(orient3d(a, b, c, {1, 1, 0}) == Orientation::COLLINEAR);
    // Near-coplanar but clearly finite inputs still resolve definitively.
    CHECK(orient3d(a, b, c, {0.0, 0.0, 1.0e-12}) == Orientation::LEFT);
}

void test_orient3d_adaptive_resolves_filter_failures() {
    // auracad's hand-constructed filter-defeating vectors (Phase 2.5b). True determinants
    // derived by exact algebra; all are O(h*delta^2) — far below the filter envelope.
    constexpr double h = static_cast<double>(1ULL << 30);            // 2^30
    constexpr double delta = 1.0 / static_cast<double>(1ULL << 22);  // 2^-22
    const Point3 a{0.0, 0.0, 0.0};

    // Case 1: det = 3*h*delta^2 + delta^3 > 0 -> LEFT.
    CHECK(orient3d(a, {h + delta, h, h}, {h, h + delta, h}, {h, h, h + delta}) ==
          Orientation::LEFT);
    // Case 2: det = -(h*delta^2 + delta^3) < 0 -> RIGHT.
    CHECK(orient3d(a, {h + delta, h, h}, {h, h + delta, h}, {h, h, h - delta}) ==
          Orientation::RIGHT);
    // Case 3: det = -(h*delta^2 + delta^3) < 0 -> RIGHT.
    CHECK(orient3d(a, {h - delta, h, h}, {h, h + delta, h}, {h, h, h + delta}) ==
          Orientation::RIGHT);
    // Case 4: det = -(3*h*delta^2 + delta^3) < 0 -> RIGHT.
    CHECK(orient3d(a, {h, h + delta, h}, {h + delta, h, h}, {h, h, h + delta}) ==
          Orientation::RIGHT);
    // Case 5: det = 3*h*delta^2 + 2*delta^3 > 0 -> LEFT.
    CHECK(orient3d(a, {h, h + delta, h + delta}, {h + delta, h, h + delta},
                   {h + delta, h + delta, h}) == Orientation::LEFT);
    // Near-coplanar at large scale: small positive volume -> LEFT.
    CHECK(orient3d(a, {h, 0.0, 0.0}, {0.0, h, 0.0}, {0.0, 0.0, delta}) == Orientation::LEFT);
}

void test_orient3d_exactly_coplanar_fixtures() {
    // Exactly representable coordinates; d lies in the integer lattice plane spanned by
    // (b - a) and (c - a), so the exact determinant is zero.
    const Point3 a{0.0, 0.0, 0.0};
    const Point3 b{2.0, 0.0, 0.0};
    const Point3 c{0.0, 2.0, 0.0};
    CHECK(orient3d(a, b, c, {64.0, -128.0, 0.0}) == Orientation::COLLINEAR);
    // Tilted exact plane z = x + y with small-integer points (all arithmetic exact).
    const Point3 p{1.0, 0.0, 1.0};
    const Point3 q{0.0, 1.0, 1.0};
    const Point3 r{1.0, 1.0, 2.0};
    const Point3 s{3.0, 5.0, 8.0};
    CHECK(orient3d(p, q, r, s) == Orientation::COLLINEAR);
    // Degenerate tetrahedron: repeated vertex.
    CHECK(orient3d(a, b, c, a) == Orientation::COLLINEAR);
    CHECK(orient3d(a, b, b, c) == Orientation::COLLINEAR);
    // All four collinear (a fortiori coplanar).
    CHECK(orient3d({1, 1, 1}, {2, 2, 2}, {4, 4, 4}, {8, 8, 8}) == Orientation::COLLINEAR);
}

void test_orient3d_ulp_perturbations() {
    // Coplanar +-1 ulp: nudge d off the plane z = 0 (and z = x + y) by one ulp; the sign
    // must follow the nudge direction. orient3d(a,b,c,d) with CCW (a,b,c) seen from +z is
    // LEFT for d above the plane.
    const double inf = std::numeric_limits<double>::infinity();
    {
        const Point3 a{0.0, 0.0, 0.0};
        const Point3 b{1.0, 0.0, 0.0};
        const Point3 c{0.0, 1.0, 0.0};
        const Point3 d{0.25, 0.25, 0.0};
        CHECK(orient3d(a, b, c, d) == Orientation::COLLINEAR);
        CHECK(orient3d(a, b, c, {d.x, d.y, std::nextafter(0.0, inf)}) == Orientation::LEFT);
        CHECK(orient3d(a, b, c, {d.x, d.y, std::nextafter(0.0, -inf)}) == Orientation::RIGHT);
    }
    {
        // Tilted plane z = x + y at a coarse scale so one ulp of z is a deep-subfilter
        // perturbation relative to the products involved.
        const Point3 p{1024.0, 0.0, 1024.0};
        const Point3 q{0.0, 1024.0, 1024.0};
        const Point3 r{1024.0, 1024.0, 2048.0};
        const Point3 s{512.0, 256.0, 768.0};
        CHECK(orient3d(p, q, r, s) == Orientation::COLLINEAR);
        const Point3 s_up{s.x, s.y, std::nextafter(s.z, inf)};
        const Point3 s_dn{s.x, s.y, std::nextafter(s.z, -inf)};
        const Orientation up = orient3d(p, q, r, s_up);
        const Orientation dn = orient3d(p, q, r, s_dn);
        CHECK(up != Orientation::COLLINEAR);
        CHECK(up != Orientation::INDETERMINATE);
        CHECK(dn == flipped(up));
    }
}

void test_orient3d_antisymmetry() {
    // Swapping two arguments flips the sign of the 3x3 determinant.
    const Point3 pts[] = {{0, 0, 0},
                          {1, 0, 0},
                          {0, 1, 0},
                          {0, 0, 1},
                          {0.3, 0.7, 0.1},
                          {1.0e6, 1.0e6, 1.0e6},
                          {1.0e6 + 1.0, 1.0e6, 1.0e6}};
    const auto n = sizeof(pts) / sizeof(pts[0]);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            for (size_t k = 0; k < n; ++k) {
                for (size_t l = 0; l < n; ++l) {
                    const Point3 &a = pts[i], &b = pts[j], &c = pts[k], &d = pts[l];
                    const Orientation o = orient3d(a, b, c, d);
                    CHECK(o != Orientation::INDETERMINATE);
                    CHECK(orient3d(b, a, c, d) == flipped(o));
                    CHECK(orient3d(a, c, b, d) == flipped(o));
                    CHECK(orient3d(a, b, d, c) == flipped(o));
                }
            }
        }
    }
}

void test_orient3d_nan_returns_indeterminate() {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    const Point3 a{0.0, 0.0, 0.0};
    const Point3 b{1.0, 0.0, 0.0};
    const Point3 c{0.0, 1.0, 0.0};
    const Point3 d{0.0, 0.0, 1.0};
    CHECK(orient3d({nan, 0.0, 0.0}, b, c, d) == Orientation::INDETERMINATE);
    CHECK(orient3d(a, {nan, 0.0, 0.0}, c, d) == Orientation::INDETERMINATE);
    CHECK(orient3d(a, b, {0.0, nan, 0.0}, d) == Orientation::INDETERMINATE);
    CHECK(orient3d(a, b, c, {0.0, 0.0, nan}) == Orientation::INDETERMINATE);
    CHECK(orient3d({inf, 0.0, 0.0}, b, c, d) == Orientation::INDETERMINATE);
    CHECK(orient3d(a, b, c, {0.0, 0.0, -inf}) == Orientation::INDETERMINATE);
}

// ---- on_segment_2d / bbox_overlap_2d (filtered-only helpers) -------------

void test_on_segment_basic() {
    const Point2 a{0.0, 0.0};
    const Point2 b{10.0, 0.0};
    CHECK(on_segment_2d(a, b, a));
    CHECK(on_segment_2d(a, b, b));
    CHECK(on_segment_2d(a, b, {5.0, 0.0}));
    CHECK(on_segment_2d(a, b, {5.0, LENGTH_EPSILON / 10.0}));
    CHECK(!on_segment_2d(a, b, {5.0, LENGTH_EPSILON * 10.0}));
    CHECK(!on_segment_2d(a, b, {11.0, 0.0}));
    CHECK(!on_segment_2d(a, b, {-1.0, 0.0}));
    const Point2 z{3.0, 4.0};
    CHECK(on_segment_2d(z, z, z));
    CHECK(!on_segment_2d(z, z, {3.0 + LENGTH_EPSILON * 10.0, 4.0}));
}

void test_bbox_overlap() {
    const AABB2 unit{0.0, 0.0, 1.0, 1.0};
    CHECK(bbox_overlap_2d(unit, unit));
    CHECK(bbox_overlap_2d(unit, AABB2{0.25, 0.25, 0.75, 0.75}));
    CHECK(bbox_overlap_2d(unit, AABB2{1.0, 0.0, 2.0, 1.0}));
    CHECK(bbox_overlap_2d(unit, AABB2{1.0, 1.0, 2.0, 2.0}));
    CHECK(!bbox_overlap_2d(unit, AABB2{2.0, 2.0, 3.0, 3.0}));
    CHECK(!bbox_overlap_2d(unit, AABB2{2.0, 0.0, 3.0, 1.0}));
    CHECK(!bbox_overlap_2d(unit, AABB2{0.0, 2.0, 1.0, 3.0}));
    const AABB2 swapped{1.0, 0.0, 0.0, 1.0};  // xmin=1, xmax=0 (caller bug)
    CHECK(!bbox_overlap_2d(swapped, AABB2{0.5, 0.5, 0.6, 0.6}));
}

// ---- incircle_2d ----------------------------------------------------------

void test_incircle_2d_basic() {
    // Unit square corners are cocircular on the circle centered at (0.5, 0.5).
    const Point2 a{0.0, 0.0};
    const Point2 b{1.0, 0.0};
    const Point2 c{1.0, 1.0};
    CHECK(incircle_2d(a, b, c, {0.0, 1.0}) == Orientation::COLLINEAR);
    CHECK(incircle_2d(a, b, c, {0.5, 0.5}) == Orientation::LEFT);
    CHECK(incircle_2d(a, b, c, {5.0, 5.0}) == Orientation::RIGHT);

    // Axis-aligned unit circle: (1,0), (0,1), (-1,0), (0,-1) are all cocircular.
    const Point2 p0{1.0, 0.0};
    const Point2 p1{0.0, 1.0};
    const Point2 p2{-1.0, 0.0};
    CHECK(incircle_2d(p0, p1, p2, {0.0, -1.0}) == Orientation::COLLINEAR);
    CHECK(incircle_2d(p0, p1, p2, {0.0, 0.0}) == Orientation::LEFT);
    CHECK(incircle_2d(p0, p1, p2, {2.0, 0.0}) == Orientation::RIGHT);
}

void test_incircle_adaptive_resolves_filter_failures() {
    // auracad's hand-constructed filter-defeating vectors (Phase 2.5c). Triangle on the
    // circle of radius R = 2^30 centered at the origin; perturbations are one ulp of R.
    constexpr double R = static_cast<double>(1ULL << 30);            // 2^30
    constexpr double delta = 1.0 / static_cast<double>(1ULL << 22);  // 2^-22
    const Point2 a{R, 0.0};
    const Point2 b{0.0, R};
    const Point2 c{-R, 0.0};

    // Case 1: slightly INSIDE (det = 2*R^2*delta*(2R - delta) > 0) -> LEFT.
    CHECK(incircle_2d(a, b, c, {0.0, -(R - delta)}) == Orientation::LEFT);
    // Case 2: slightly OUTSIDE (det = -2*R^2*delta*(2R + delta) < 0) -> RIGHT.
    CHECK(incircle_2d(a, b, c, {0.0, -(R + delta)}) == Orientation::RIGHT);

    // Rotated triangle, same circumcircle; perturb along x.
    const Point2 a2{0.0, R};
    const Point2 b2{-R, 0.0};
    const Point2 c2{0.0, -R};
    CHECK(incircle_2d(a2, b2, c2, {R - delta, 0.0}) == Orientation::LEFT);
    CHECK(incircle_2d(a2, b2, c2, {R + delta, 0.0}) == Orientation::RIGHT);

    // Case 5: tangential perturbation (det = -2*R^2*delta^2, deep in the noise floor).
    CHECK(incircle_2d(a, b, c, {delta, -R}) == Orientation::RIGHT);
}

void test_incircle_exactly_cocircular_fixtures() {
    // Pythagorean lattice points on the radius-5 circle centered at the origin: all of
    // (±3,±4), (±4,±3), (±5,0), (0,±5) are exactly cocircular and exactly representable.
    const Point2 q0{5.0, 0.0};
    const Point2 q1{3.0, 4.0};
    const Point2 q2{-4.0, 3.0};
    CHECK(incircle_2d(q0, q1, q2, {0.0, -5.0}) == Orientation::COLLINEAR);
    CHECK(incircle_2d(q0, q1, q2, {-3.0, -4.0}) == Orientation::COLLINEAR);
    CHECK(incircle_2d(q0, q1, q2, {4.0, 3.0}) == Orientation::COLLINEAR);
    CHECK(incircle_2d(q0, q1, q2, {0.0, 0.0}) == Orientation::LEFT);
    CHECK(incircle_2d(q0, q1, q2, {6.0, 0.0}) == Orientation::RIGHT);

    // Same circle scaled by a power of two (exact) and offset by exact integers.
    const Point2 r0{160.0 + 7.0, -64.0};
    const Point2 r1{96.0 + 7.0, 128.0 - 64.0};
    const Point2 r2{-128.0 + 7.0, 96.0 - 64.0};
    const Point2 r_on{7.0, -160.0 - 64.0};
    CHECK(incircle_2d(r0, r1, r2, r_on) == Orientation::COLLINEAR);
}

void test_incircle_ulp_perturbations() {
    // Exactly cocircular, then nudge d radially by one ulp: inward -> INSIDE (LEFT),
    // outward -> OUTSIDE (RIGHT), for a CCW triangle.
    const double inf = std::numeric_limits<double>::infinity();
    const Point2 a{5.0, 0.0};
    const Point2 b{0.0, 5.0};
    const Point2 c{-5.0, 0.0};
    const Point2 d{0.0, -5.0};
    CHECK(incircle_2d(a, b, c, d) == Orientation::COLLINEAR);
    // d at (0, -5): moving y toward 0 is radially inward; away from 0 is outward.
    CHECK(incircle_2d(a, b, c, {0.0, std::nextafter(-5.0, 0.0)}) == Orientation::LEFT);
    CHECK(incircle_2d(a, b, c, {0.0, std::nextafter(-5.0, -inf)}) == Orientation::RIGHT);
    // Same at a lattice point not on an axis: (3, -4); scale radius by moving both
    // coordinates is overkill — one-ulp change of one coordinate already changes |d|.
    const Point2 e{3.0, -4.0};
    CHECK(incircle_2d(a, b, c, e) == Orientation::COLLINEAR);
    CHECK(incircle_2d(a, b, c, {e.x, std::nextafter(e.y, 0.0)}) == Orientation::LEFT);
    CHECK(incircle_2d(a, b, c, {e.x, std::nextafter(e.y, -inf)}) == Orientation::RIGHT);
}

void test_incircle_symmetry_properties() {
    // Swapping two of the triangle vertices flips the sign (Shewchuk's convention: the
    // result is relative to the orientation of (a, b, c)).
    const Point2 a{5.0, 0.0};
    const Point2 b{0.0, 5.0};
    const Point2 c{-5.0, 0.0};
    const Point2 ds[] = {{0.0, 0.0}, {6.0, 0.0},  {0.0, -5.0},
                         {1.0, 1.0}, {3.0, -4.0}, {4.99999999, 0.0}};
    for (const auto& d : ds) {
        const Orientation o = incircle_2d(a, b, c, d);
        CHECK(o != Orientation::INDETERMINATE);
        CHECK(incircle_2d(b, a, c, d) == flipped(o));
        CHECK(incircle_2d(a, c, b, d) == flipped(o));
        CHECK(incircle_2d(c, b, a, d) == flipped(o));
        // Cyclic rotations preserve the sign.
        CHECK(incircle_2d(b, c, a, d) == o);
        CHECK(incircle_2d(c, a, b, d) == o);
    }
}

void test_incircle_nan_returns_indeterminate() {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    const Point2 a{0.0, 0.0};
    const Point2 b{1.0, 0.0};
    const Point2 c{0.0, 1.0};
    const Point2 d{0.3, 0.3};
    CHECK(incircle_2d({nan, 0.0}, b, c, d) == Orientation::INDETERMINATE);
    CHECK(incircle_2d(a, {nan, 0.0}, c, d) == Orientation::INDETERMINATE);
    CHECK(incircle_2d(a, b, {0.0, nan}, d) == Orientation::INDETERMINATE);
    CHECK(incircle_2d(a, b, c, {nan, 0.0}) == Orientation::INDETERMINATE);
    CHECK(incircle_2d(a, b, c, {0.0, nan}) == Orientation::INDETERMINATE);
    CHECK(incircle_2d({inf, 0.0}, b, c, d) == Orientation::INDETERMINATE);
    CHECK(incircle_2d(a, b, c, {0.0, -inf}) == Orientation::INDETERMINATE);
    CHECK(incircle_2d(a, b, c, {inf, inf}) == Orientation::INDETERMINATE);
    // Finite near-cocircular case MUST NOT return INDETERMINATE.
    CHECK(incircle_2d(a, b, c, d) != Orientation::INDETERMINATE);
}

void run() {
    test_orient2d_basic();
    test_orient2d_known_signs();
    test_orient2d_degenerate_filter();
    test_orient2d_adaptive_resolves_filter_failures();
    test_orient2d_exactly_collinear_fixtures();
    test_orient2d_ulp_perturbations();
    test_orient2d_antisymmetry();
    test_orient2d_nan_returns_indeterminate();
    test_orient3d_basic();
    test_orient3d_adaptive_resolves_filter_failures();
    test_orient3d_exactly_coplanar_fixtures();
    test_orient3d_ulp_perturbations();
    test_orient3d_antisymmetry();
    test_orient3d_nan_returns_indeterminate();
    test_on_segment_basic();
    test_bbox_overlap();
    test_incircle_2d_basic();
    test_incircle_adaptive_resolves_filter_failures();
    test_incircle_exactly_cocircular_fixtures();
    test_incircle_ulp_perturbations();
    test_incircle_symmetry_properties();
    test_incircle_nan_returns_indeterminate();
}

}  // namespace

TEST_MAIN_RUN()
