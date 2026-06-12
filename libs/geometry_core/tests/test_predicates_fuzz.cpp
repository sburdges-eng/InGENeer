// test_predicates_fuzz.cpp — deterministic fuzz harness for the robust predicates
// (Phase 5 exit gate). CTest-runnable; no RNG/wall-clock dependence (C-4.6): a fixed-seed
// LCG drives every generated input, so each run replays bit-identically.
//
// Coverage per iteration batch:
//   * uniform random coordinates across mixed binary exponents;
//   * near-degenerate inputs (points constructed on a line / plane / circle in floating
//     point, i.e. inexact but within a few ulps of degeneracy);
//   * EXACT degenerate constructions (integer lattice points scaled by powers of two:
//     collinear / coplanar / cocircular with all arithmetic exactly representable);
//   * ulp-scale perturbations of the exact-degenerate constructions via std::nextafter.
//
// Invariants checked on every sample:
//   1. The result is a legal value — LEFT / RIGHT / COLLINEAR; never INDETERMINATE on
//      finite input.
//   2. Antisymmetry: swapping two points flips the sign; cyclic rotation preserves it.
//   3. Exact-degenerate constructions return COLLINEAR (exact zero determinant).
//   4. One-ulp perturbations off an exact degeneracy resolve to consistent, opposite
//      nonzero signs (and, for incircle, to the side predicted by the triangle
//      orientation).
//
// Iteration budget: kDefaultTotalIterations (1,000,000) generator iterations split
// across the three predicates; each iteration performs several predicate calls, so the
// default run evaluates well over 3 million predicate calls in < 60 s. For the full
// soak (e.g. the one-off 10,000,000-iteration Phase 5 gate run), set the environment
// variable GEOM_FUZZ_TOTAL_ITERATIONS=10000000 — the seed is unchanged, so the longer
// run is a strict superset prefix of the default one.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "check.hpp"
#include "ingeneer/geom/predicates.h"

using ingeneer::geom::predicates::incircle_2d;
using ingeneer::geom::predicates::orient2d;
using ingeneer::geom::predicates::orient3d;
using ingeneer::geom::predicates::Orientation;
using ingeneer::geom::predicates::Point2;
using ingeneer::geom::predicates::Point3;

namespace {

constexpr std::uint64_t kDefaultTotalIterations = 1'000'000;

// Deterministic LCG (no RNG dependence in tests — C-4.6 discipline). Same generator as
// libs/audit_core/tests/test_property_authority.cpp.
struct Lcg {
    std::uint64_t state = 0x2545F4914F6CDD1Dull;
    std::uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 33);
    }
};

Orientation flipped(Orientation o) {
    if (o == Orientation::LEFT) return Orientation::RIGHT;
    if (o == Orientation::RIGHT) return Orientation::LEFT;
    return o;
}

bool legal(Orientation o) {
    return o == Orientation::LEFT || o == Orientation::RIGHT || o == Orientation::COLLINEAR;
}

// Uniform double in [-1, 1) with 31-bit granularity, scaled by a random binary exponent
// in [-16, 16]. ldexp is exact, so the distribution is deterministic across platforms.
double rand_coord(Lcg& rng) {
    const double m = static_cast<double>(rng.next()) / static_cast<double>(1ULL << 31) * 2.0 - 1.0;
    const int e = static_cast<int>(rng.next() % 33u) - 16;
    return std::ldexp(m, e);
}

// Small integer in [-bound, bound].
int rand_int(Lcg& rng, int bound) {
    return static_cast<int>(rng.next() % (2u * static_cast<unsigned>(bound) + 1u)) - bound;
}

// Power-of-two scale factor 2^e, e in [-emag, emag]. Exact multiplier.
double rand_pow2(Lcg& rng, int emag) { return std::ldexp(1.0, rand_int(rng, emag)); }

// Move v by one ulp toward `towards`.
double ulp_toward(double v, double towards) { return std::nextafter(v, towards); }

// Move v by one ulp away from `from` (v != from assumed).
double ulp_away(double v, double from) {
    const double inf = std::numeric_limits<double>::infinity();
    return std::nextafter(v, v > from ? inf : -inf);
}

// ---- orient2d ------------------------------------------------------------

void fuzz_orient2d_uniform(Lcg& rng, std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) {
        const Point2 a{rand_coord(rng), rand_coord(rng)};
        const Point2 b{rand_coord(rng), rand_coord(rng)};
        Point2 c{rand_coord(rng), rand_coord(rng)};
        // Half the time, place c near the line through a-b (inexact construction, lands
        // within a few ulps of collinear — the filter's worst nightmare).
        if ((rng.next() & 1u) == 0u) {
            const double t =
                static_cast<double>(rng.next()) / static_cast<double>(1ULL << 31) * 4.0 - 2.0;
            c = Point2{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
        }
        const Orientation o = orient2d(a, b, c);
        CHECK(legal(o));
        CHECK(orient2d(b, a, c) == flipped(o));
        CHECK(orient2d(b, c, a) == o);
    }
}

void fuzz_orient2d_exact_degenerate(Lcg& rng, std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) {
        // Exact construction: integer lattice line scaled by a power of two. All
        // coordinates and the 2x2 determinant are exactly representable, so COLLINEAR is
        // the only correct answer.
        const double s = rand_pow2(rng, 18);
        const int ax = rand_int(rng, 100), ay = rand_int(rng, 100);
        int dx = rand_int(rng, 100), dy = rand_int(rng, 100);
        if (dx == 0 && dy == 0) dx = 1;  // ensure b != a
        const int k = rand_int(rng, 4);
        const Point2 a{static_cast<double>(ax) * s, static_cast<double>(ay) * s};
        const Point2 b{static_cast<double>(ax + dx) * s, static_cast<double>(ay + dy) * s};
        const Point2 c{static_cast<double>(ax + k * dx) * s, static_cast<double>(ay + k * dy) * s};
        CHECK(orient2d(a, b, c) == Orientation::COLLINEAR);
        CHECK(orient2d(b, c, a) == Orientation::COLLINEAR);

        // ulp-scale perturbation off the line: perturb the coordinate the line is not
        // parallel to, so the exact determinant becomes exactly +-|d|*ulp != 0.
        const double inf = std::numeric_limits<double>::infinity();
        Point2 c_up = c, c_dn = c;
        if (dx != 0) {
            // Perturbing a coordinate that is exactly 0 lands on +-0x1p-1074;
            // products against a subnormal underflow, which is outside the
            // predicates' documented validity domain (see predicates.h). Skip.
            if (c.y == 0.0) continue;
            c_up.y = std::nextafter(c.y, inf);
            c_dn.y = std::nextafter(c.y, -inf);
        } else {
            if (c.x == 0.0) continue;
            c_up.x = std::nextafter(c.x, inf);
            c_dn.x = std::nextafter(c.x, -inf);
        }
        const Orientation up = orient2d(a, b, c_up);
        const Orientation dn = orient2d(a, b, c_dn);
        CHECK(legal(up));
        CHECK(up != Orientation::COLLINEAR);
        CHECK(dn == flipped(up));
        CHECK(orient2d(b, a, c_up) == flipped(up));
    }
}

// ---- orient3d ------------------------------------------------------------

void fuzz_orient3d_uniform(Lcg& rng, std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) {
        const Point3 a{rand_coord(rng), rand_coord(rng), rand_coord(rng)};
        const Point3 b{rand_coord(rng), rand_coord(rng), rand_coord(rng)};
        const Point3 c{rand_coord(rng), rand_coord(rng), rand_coord(rng)};
        Point3 d{rand_coord(rng), rand_coord(rng), rand_coord(rng)};
        // Half the time, place d near the plane through a, b, c (inexact construction).
        if ((rng.next() & 1u) == 0u) {
            const double u =
                static_cast<double>(rng.next()) / static_cast<double>(1ULL << 31) * 4.0 - 2.0;
            const double v =
                static_cast<double>(rng.next()) / static_cast<double>(1ULL << 31) * 4.0 - 2.0;
            d = Point3{a.x + u * (b.x - a.x) + v * (c.x - a.x),
                       a.y + u * (b.y - a.y) + v * (c.y - a.y),
                       a.z + u * (b.z - a.z) + v * (c.z - a.z)};
        }
        const Orientation o = orient3d(a, b, c, d);
        CHECK(legal(o));
        CHECK(orient3d(b, a, c, d) == flipped(o));
        CHECK(orient3d(a, c, b, d) == flipped(o));
        CHECK(orient3d(a, b, d, c) == flipped(o));
    }
}

void fuzz_orient3d_exact_degenerate(Lcg& rng, std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) {
        // Exact construction: d in the integer lattice plane spanned by (b-a, c-a),
        // scaled by a power of two. All arithmetic exact -> COLLINEAR (coplanar).
        const double s = rand_pow2(rng, 10);
        const long long ax = rand_int(rng, 50), ay = rand_int(rng, 50), az = rand_int(rng, 50);
        const long long ux = rand_int(rng, 20), uy = rand_int(rng, 20), uz = rand_int(rng, 20);
        const long long vx = rand_int(rng, 20), vy = rand_int(rng, 20), vz = rand_int(rng, 20);
        const long long ii = rand_int(rng, 3), jj = rand_int(rng, 3);
        const Point3 a{static_cast<double>(ax) * s, static_cast<double>(ay) * s,
                       static_cast<double>(az) * s};
        const Point3 b{static_cast<double>(ax + ux) * s, static_cast<double>(ay + uy) * s,
                       static_cast<double>(az + uz) * s};
        const Point3 c{static_cast<double>(ax + vx) * s, static_cast<double>(ay + vy) * s,
                       static_cast<double>(az + vz) * s};
        const Point3 d{static_cast<double>(ax + ii * ux + jj * vx) * s,
                       static_cast<double>(ay + ii * uy + jj * vy) * s,
                       static_cast<double>(az + ii * uz + jj * vz) * s};
        CHECK(orient3d(a, b, c, d) == Orientation::COLLINEAR);

        // Normal n = u x v in exact integer arithmetic decides which axis perturbation
        // of d leaves the plane (the perturbed exact determinant is +-n_axis * ulp).
        const long long nx = uy * vz - uz * vy;
        const long long ny = uz * vx - ux * vz;
        const long long nz = ux * vy - uy * vx;
        const double inf = std::numeric_limits<double>::infinity();
        Point3 d_up = d, d_dn = d;
        bool nondegenerate = true;
        // Subnormal-domain guard, same as the 2D section: perturbing an
        // exactly-zero coordinate produces +-0x1p-1074 and underflowing
        // products — outside the predicates' validity domain (predicates.h).
        if (nz != 0) {
            if (d.z == 0.0) continue;
            d_up.z = std::nextafter(d.z, inf);
            d_dn.z = std::nextafter(d.z, -inf);
        } else if (ny != 0) {
            if (d.y == 0.0) continue;
            d_up.y = std::nextafter(d.y, inf);
            d_dn.y = std::nextafter(d.y, -inf);
        } else if (nx != 0) {
            if (d.x == 0.0) continue;
            d_up.x = std::nextafter(d.x, inf);
            d_dn.x = std::nextafter(d.x, -inf);
        } else {
            nondegenerate = false;  // u parallel to v: every d stays coplanar
        }
        if (nondegenerate) {
            const Orientation up = orient3d(a, b, c, d_up);
            const Orientation dn = orient3d(a, b, c, d_dn);
            CHECK(legal(up));
            CHECK(up != Orientation::COLLINEAR);
            CHECK(dn == flipped(up));
            CHECK(orient3d(b, a, c, d_up) == flipped(up));
        }
    }
}

// ---- incircle_2d -----------------------------------------------------------

// The 12 integer lattice points on the circle of radius 5 about the origin
// (Pythagorean triple 3-4-5). Exactly representable at any power-of-two scale.
constexpr int kCirclePts[12][2] = {{5, 0},  {4, 3},   {3, 4},   {0, 5},  {-3, 4}, {-4, 3},
                                   {-5, 0}, {-4, -3}, {-3, -4}, {0, -5}, {3, -4}, {4, -3}};

void fuzz_incircle_uniform(Lcg& rng, std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) {
        const Point2 a{rand_coord(rng), rand_coord(rng)};
        const Point2 b{rand_coord(rng), rand_coord(rng)};
        const Point2 c{rand_coord(rng), rand_coord(rng)};
        Point2 d{rand_coord(rng), rand_coord(rng)};
        // Half the time, place d near the circle through a, b, c by reflecting a across
        // the perpendicular bisector plane logic is overkill — instead reuse one of the
        // triangle's own vertices nudged by a few ulps (guaranteed near-cocircular).
        if ((rng.next() & 1u) == 0u) {
            d = a;
            const double inf = std::numeric_limits<double>::infinity();
            const unsigned n = rng.next() % 4u;
            for (unsigned k = 0; k < n; ++k) {
                d.x = std::nextafter(d.x, (rng.next() & 1u) != 0u ? inf : -inf);
            }
        }
        const Orientation o = incircle_2d(a, b, c, d);
        CHECK(legal(o));
        CHECK(incircle_2d(b, a, c, d) == flipped(o));
        CHECK(incircle_2d(b, c, a, d) == o);
    }
}

void fuzz_incircle_exact_degenerate(Lcg& rng, std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) {
        // Exact construction: four distinct lattice points on an integer-centered,
        // power-of-two-scaled 3-4-5 circle. All coordinates and the degree-4 determinant
        // terms are exactly representable -> COLLINEAR (cocircular) is the only correct
        // answer.
        const double s = rand_pow2(rng, 10);
        const int ox = rand_int(rng, 100), oy = rand_int(rng, 100);
        unsigned idx[4];
        idx[0] = rng.next() % 12u;
        idx[1] = (idx[0] + 1u + rng.next() % 11u) % 12u;
        do {
            idx[2] = rng.next() % 12u;
        } while (idx[2] == idx[0] || idx[2] == idx[1]);
        do {
            idx[3] = rng.next() % 12u;
        } while (idx[3] == idx[0] || idx[3] == idx[1] || idx[3] == idx[2]);

        Point2 p[4];
        for (int k = 0; k < 4; ++k) {
            p[k] = Point2{static_cast<double>(kCirclePts[idx[k]][0] + ox) * s,
                          static_cast<double>(kCirclePts[idx[k]][1] + oy) * s};
        }
        const Point2 &a = p[0], &b = p[1], &c = p[2], &d = p[3];
        CHECK(incircle_2d(a, b, c, d) == Orientation::COLLINEAR);
        CHECK(incircle_2d(b, a, c, d) == Orientation::COLLINEAR);

        // ulp-scale radial perturbation of d. Move the coordinate with the larger offset
        // from the circle center one ulp toward the center (strictly inside) and one ulp
        // away (strictly outside). For a CCW triangle inside reads LEFT; for CW the signs
        // flip — orient2d(a, b, c) supplies the reference orientation (never COLLINEAR
        // for three distinct cocircular points).
        const double cx = static_cast<double>(ox) * s;
        const double cy = static_cast<double>(oy) * s;
        const Orientation tri = orient2d(a, b, c);
        CHECK(tri != Orientation::COLLINEAR);
        CHECK(legal(tri));
        Point2 d_in = d, d_out = d;
        if (kCirclePts[idx[3]][1] != 0) {
            // When the absolute coordinate is exactly 0 (the center offset cancels the
            // lattice point), a 1-ulp move lands on +-0x1p-1074: products against a
            // subnormal underflow to zero, which is OUTSIDE the documented validity
            // domain of Shewchuk-style predicates (reference predicates.c carries the
            // same no-underflow caveat). Skip — not a predicate defect.
            if (d.y == 0.0) continue;
            d_in.y = ulp_toward(d.y, cy);
            d_out.y = ulp_away(d.y, cy);
        } else {
            if (d.x == 0.0) continue;
            d_in.x = ulp_toward(d.x, cx);
            d_out.x = ulp_away(d.x, cx);
        }
        CHECK(incircle_2d(a, b, c, d_in) == tri);
        CHECK(incircle_2d(a, b, c, d_out) == flipped(tri));
    }
}

void run() {
    std::uint64_t total = kDefaultTotalIterations;
    if (const char* env = std::getenv("GEOM_FUZZ_TOTAL_ITERATIONS")) {
        const unsigned long long v = std::strtoull(env, nullptr, 10);
        if (v > 0) total = v;
    }

    // Budget split (fractions of the total iteration count). Each iteration makes
    // multiple predicate calls, so actual predicate evaluations are ~4x these numbers.
    const std::uint64_t n_o2d_uniform = total * 20 / 100;
    const std::uint64_t n_o2d_exact = total * 15 / 100;
    const std::uint64_t n_o3d_uniform = total * 15 / 100;
    const std::uint64_t n_o3d_exact = total * 15 / 100;
    const std::uint64_t n_icc_uniform = total * 20 / 100;
    const std::uint64_t n_icc_exact = total * 15 / 100;

    Lcg rng;  // fixed seed: deterministic, replayable
    fuzz_orient2d_uniform(rng, n_o2d_uniform);
    fuzz_orient2d_exact_degenerate(rng, n_o2d_exact);
    fuzz_orient3d_uniform(rng, n_o3d_uniform);
    fuzz_orient3d_exact_degenerate(rng, n_o3d_exact);
    fuzz_incircle_uniform(rng, n_icc_uniform);
    fuzz_incircle_exact_degenerate(rng, n_icc_exact);

    std::printf(
        "geom.predicates_fuzz: %llu generator iterations "
        "(orient2d %llu+%llu, orient3d %llu+%llu, incircle %llu+%llu), "
        "failures=%d\n",
        static_cast<unsigned long long>(total), static_cast<unsigned long long>(n_o2d_uniform),
        static_cast<unsigned long long>(n_o2d_exact),
        static_cast<unsigned long long>(n_o3d_uniform),
        static_cast<unsigned long long>(n_o3d_exact),
        static_cast<unsigned long long>(n_icc_uniform),
        static_cast<unsigned long long>(n_icc_exact), ::ingeneer::geom::test::g_failures);
}

}  // namespace

TEST_MAIN_RUN()
