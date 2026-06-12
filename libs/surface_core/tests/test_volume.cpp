// surface_core volumes — analytic known answers + pinned numpy cross-check (Phase 6.3).
//
// TIN-prism vs hand-computed tetrahedron / pyramid, exact mixed-triangle cut/fill
// splitting, signed cut-vs-fill convention, shared-support surface-to-surface volumes,
// independent-triangulation overlay volumes (general volume_between), prismoidal vs
// analytic cross-sections (average-end-area as labeled report option).
//
// Oracle: TOTaLi has no volume pipeline (groundtruthos-data/pipeline/features.py only
// reads precomputed metadata), so per ADR-0023 the corpus-scale quantities are a
// scipy/numpy cross-check pinned by tools/oracle/extract_from_totali.py into the cv
// fixture. argv[1] is the TIN fixture (points), argv[2] the contour/volume fixture.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "ingeneer/surface/tin.h"
#include "ingeneer/surface/volume.h"

using namespace ingeneer::surface;

namespace {

const char* g_points_path = nullptr;
const char* g_cv_path = nullptr;

double hexd(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

// Square pyramid: 2x2 base at z = 0, apex (1,1,3). Footprint area 4, volume 4.
Tin pyramid() {
    Tin tin;
    CHECK(tin.insert(0, 0, 0).has_value());
    CHECK(tin.insert(2, 0, 0).has_value());
    CHECK(tin.insert(2, 2, 0).has_value());
    CHECK(tin.insert(0, 2, 0).has_value());
    CHECK(tin.insert(1, 1, 3).has_value());
    CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(4));
    return tin;
}

void test_tetra_prism() {
    // Single triangle (area 1/2) with one vertex at z = 3: the solid above z = 0 is a
    // tetrahedron, V = base * h / 3 = (1/2)(3)/3 = 1/2... computed per prism formula
    // A * (d1+d2+d3)/3 = 0.5 * (3+0+0)/3 = 0.5. Hand-computed.
    Tin tin;
    CHECK(tin.insert(0, 0, 3).has_value());
    CHECK(tin.insert(1, 0, 0).has_value());
    CHECK(tin.insert(0, 1, 0).has_value());
    auto r = volume_to_plane(tin, 0.0);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 0.5) < 1e-12);
    CHECK(r->fill == 0.0);
    CHECK(std::fabs(r->area - 0.5) < 1e-12);  // plan footprint of the single triangle
}

void test_pyramid_prism_and_mixed_split() {
    Tin tin = pyramid();

    // Whole pyramid above its base plane: V = 4 * 3 / 3 = 4. All cut.
    auto base = volume_to_plane(tin, 0.0);
    CHECK(base.has_value());
    CHECK(std::fabs(base->cut - 4.0) < 1e-12);
    CHECK(base->fill == 0.0);
    CHECK(std::fabs(base->area - 4.0) < 1e-12);  // 2x2 plan footprint

    // Plane z = 1 slices every fan triangle (mixed signs): cut is the similar pyramid
    // above the plane, scale 2/3 => V = 4 * (2/3)^3 = 32/27; net = 4 - 1*4 = 0 so
    // fill = cut. Exercises the exact d = 0 clipping.
    auto mid = volume_to_plane(tin, 1.0);
    CHECK(mid.has_value());
    CHECK(std::fabs(mid->cut - 32.0 / 27.0) < 1e-12);
    CHECK(std::fabs(mid->fill - 32.0 / 27.0) < 1e-12);
    CHECK(std::fabs(mid->net()) < 1e-12);

    // Plane through the apex: nothing above it (apex d == 0 contributes no volume).
    auto top = volume_to_plane(tin, 3.0);
    CHECK(top.has_value());
    CHECK(top->cut == 0.0);
    CHECK(std::fabs(top->fill - (3.0 * 4.0 - 4.0)) < 1e-12);

    auto nan = volume_to_plane(tin, std::nan(""));
    CHECK(!nan.has_value());
    if (!nan.has_value()) CHECK(nan.error().code == VolumeErrc::NonFiniteInput);
}

void test_sign_convention() {
    Tin tin = pyramid();  // z in [0, 3], footprint area 4, volume 4

    // Surface entirely ABOVE the reference plane => cut only, net positive (excavation).
    auto below = volume_to_plane(tin, -2.0);
    CHECK(below.has_value());
    CHECK(std::fabs(below->cut - (2.0 * 4.0 + 4.0)) < 1e-12);
    CHECK(below->fill == 0.0);
    CHECK(below->net() > 0.0);

    // Surface entirely BELOW the reference plane => fill only, net negative.
    auto above = volume_to_plane(tin, 10.0);
    CHECK(above.has_value());
    CHECK(above->cut == 0.0);
    CHECK(std::fabs(above->fill - (10.0 * 4.0 - 4.0)) < 1e-12);
    CHECK(above->net() < 0.0);
}

void test_surface_to_surface_shared_support() {
    Tin design = pyramid();

    // Existing = design raised by 0.5 everywhere: cut = 0.5 * footprint = 2, fill = 0.
    Tin raised;
    CHECK(raised.insert(0, 0, 0.5).has_value());
    CHECK(raised.insert(2, 0, 0.5).has_value());
    CHECK(raised.insert(2, 2, 0.5).has_value());
    CHECK(raised.insert(0, 2, 0.5).has_value());
    CHECK(raised.insert(1, 1, 3.5).has_value());
    auto r = volume_between(design, raised);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 2.0) < 1e-12);
    CHECK(r->fill == 0.0);
    CHECK(std::fabs(r->area - 4.0) < 1e-12);  // full shared footprint

    // Swapped roles: existing below design => fill.
    auto swapped = volume_between(raised, design);
    CHECK(swapped.has_value());
    CHECK(swapped->cut == 0.0);
    CHECK(std::fabs(swapped->fill - 2.0) < 1e-12);

    // Unshared support now routes through the overlay path (Phase 6.3 deferred item):
    // existing = flat z = 0 over triangle (0,0)-(2,0)-(0,2), i.e. the half of the pyramid
    // footprint below the diagonal x + y = 2 (which passes through the apex plan position
    // (1,1)). Pyramid volume over that half is 2 by symmetry; existing is below design
    // everywhere => fill = 2 exactly, cut = 0, overlap area = 2. The two fan triangles of
    // the pyramid on the far side of the diagonal meet the overlap region only in a
    // segment and must contribute exactly zero.
    Tin fewer;
    CHECK(fewer.insert(0, 0, 0).has_value());
    CHECK(fewer.insert(2, 0, 0).has_value());
    CHECK(fewer.insert(0, 2, 0).has_value());
    auto half = volume_between(design, fewer);
    CHECK(half.has_value());
    CHECK(half->cut == 0.0);
    CHECK(std::fabs(half->fill - 2.0) < 1e-12);
    CHECK(std::fabs(half->area - 2.0) < 1e-12);
}

void test_mixed_triangle_exact_split() {
    // Single triangle, d = (-1, -1, 3) vs plane 0; plan area 8. Hand computation:
    // positive sub-triangle (3,1)-(0,4)-(0,1), area 4.5 => cut = 4.5 * (3/3) = 4.5;
    // signed prism = 8 * (−1−1+3)/3 = 8/3; fill = cut − net = 4.5 − 8/3 = 11/6.
    Tin tin;
    CHECK(tin.insert(0, 0, -1).has_value());
    CHECK(tin.insert(4, 0, -1).has_value());
    CHECK(tin.insert(0, 4, 3).has_value());
    auto r = volume_to_plane(tin, 0.0);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 4.5) < 1e-12);
    CHECK(std::fabs(r->fill - 11.0 / 6.0) < 1e-12);
    CHECK(std::fabs(r->net() - 8.0 / 3.0) < 1e-12);
}

// ---- independent-triangulation overlay (general volume_between) ------------------------

// 2-triangle square [0,s]^2 at constant z (corners only).
Tin flat_square(double s, double z) {
    Tin tin;
    CHECK(tin.insert(0, 0, z).has_value());
    CHECK(tin.insert(s, 0, z).has_value());
    CHECK(tin.insert(s, s, z).has_value());
    CHECK(tin.insert(0, s, z).has_value());
    CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(2));
    return tin;
}

void test_independent_flat_planes() {
    // Two flat planes over [0,4]^2 with DIFFERENT triangulations: design = 2-triangle
    // corner square at z = 1; existing = 8-triangle grid (corners + edge midpoints +
    // centre) at z = 3. Volume = area * dz = 16 * 2 = 32, all cut (existing above).
    Tin design = flat_square(4, 1.0);
    Tin existing;
    CHECK(existing.insert(0, 0, 3).has_value());
    CHECK(existing.insert(4, 0, 3).has_value());
    CHECK(existing.insert(4, 4, 3).has_value());
    CHECK(existing.insert(0, 4, 3).has_value());
    CHECK(existing.insert(2, 0, 3).has_value());
    CHECK(existing.insert(4, 2, 3).has_value());
    CHECK(existing.insert(2, 4, 3).has_value());
    CHECK(existing.insert(0, 2, 3).has_value());
    CHECK(existing.insert(2, 2, 3).has_value());
    CHECK_EQ(existing.triangle_count(), static_cast<std::size_t>(8));

    auto r = volume_between(design, existing);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 32.0) < 1e-12);
    CHECK(r->fill == 0.0);
    CHECK(std::fabs(r->area - 16.0) < 1e-12);

    // Swapped roles: existing below design => all fill.
    auto sw = volume_between(existing, design);
    CHECK(sw.has_value());
    CHECK(sw->cut == 0.0);
    CHECK(std::fabs(sw->fill - 32.0) < 1e-12);
    CHECK(std::fabs(sw->area - 16.0) < 1e-12);
}

void test_independent_tilted_vs_flat() {
    // design = flat z = 0 over [0,2]^2 (2 triangles); existing = the plane z = x sampled
    // at the corners plus the centre (4-triangle fan). Closed form:
    // cut = integral of x over [0,2]^2 = 4; fill = 0 (d = x >= 0; the x = 0 edge of the
    // overlap is a zero-area boundary and contributes exactly nothing).
    Tin design = flat_square(2, 0.0);
    Tin existing;
    CHECK(existing.insert(0, 0, 0).has_value());
    CHECK(existing.insert(2, 0, 2).has_value());
    CHECK(existing.insert(2, 2, 2).has_value());
    CHECK(existing.insert(0, 2, 0).has_value());
    CHECK(existing.insert(1, 1, 1).has_value());
    CHECK_EQ(existing.triangle_count(), static_cast<std::size_t>(4));

    auto r = volume_between(design, existing);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 4.0) < 1e-12);
    CHECK(r->fill == 0.0);
    CHECK(std::fabs(r->area - 4.0) < 1e-12);
}

void test_independent_crossing_surfaces() {
    // design = flat z = 0 over [0,2]^2; existing = plane z = x - 1 (corners + centre
    // fan): the difference changes sign at x = 1, INSIDE faces of both triangulations.
    // cut = integral of max(x-1, 0) = 1, fill = integral of max(1-x, 0) = 1, net = 0.
    Tin design = flat_square(2, 0.0);
    Tin existing;
    CHECK(existing.insert(0, 0, -1).has_value());
    CHECK(existing.insert(2, 0, 1).has_value());
    CHECK(existing.insert(2, 2, 1).has_value());
    CHECK(existing.insert(0, 2, -1).has_value());
    CHECK(existing.insert(1, 1, 0).has_value());

    auto r = volume_between(design, existing);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 1.0) < 1e-12);
    CHECK(std::fabs(r->fill - 1.0) < 1e-12);
    CHECK(std::fabs(r->net()) < 1e-12);
    CHECK(std::fabs(r->area - 4.0) < 1e-12);
}

void test_independent_partial_overlap() {
    // Hulls overlap in a strict sub-region: design = flat z = 0 over [0,4]^2; existing =
    // flat z = 1 over [2,6]x[0,4] (corners + centre). Volumes are computed over the hull
    // INTERSECTION [2,4]x[0,4] only: area 8, cut 8, fill 0.
    Tin design = flat_square(4, 0.0);
    Tin existing;
    CHECK(existing.insert(2, 0, 1).has_value());
    CHECK(existing.insert(6, 0, 1).has_value());
    CHECK(existing.insert(6, 4, 1).has_value());
    CHECK(existing.insert(2, 4, 1).has_value());
    CHECK(existing.insert(4, 2, 1).has_value());

    auto r = volume_between(design, existing);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 8.0) < 1e-12);
    CHECK(r->fill == 0.0);
    CHECK(std::fabs(r->area - 8.0) < 1e-12);
}

void test_general_path_matches_shared_support() {
    // Same two planes (design z = 1 + x/4 + y/2, existing z = 3 - x/2 + y/4) over
    // [0,4]^2, once with SHARED support (fast path) and once with the existing surface
    // resampled with an extra centre vertex (general overlay path). Identical geometric
    // surfaces => identical volumes within 1e-9 relative. The difference field
    // d = 2 - 3x/4 - y/4 changes sign inside the square, so both cut and fill are
    // nonzero and net is exactly 0 (d is antisymmetric about the centre).
    auto zd = [](double x, double y) { return 1.0 + 0.25 * x + 0.5 * y; };
    auto ze = [](double x, double y) { return 3.0 - 0.5 * x + 0.25 * y; };
    Tin design, shared, resampled;
    const double corners[4][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    for (const auto& p : corners) {
        CHECK(design.insert(p[0], p[1], zd(p[0], p[1])).has_value());
        CHECK(shared.insert(p[0], p[1], ze(p[0], p[1])).has_value());
        CHECK(resampled.insert(p[0], p[1], ze(p[0], p[1])).has_value());
    }
    CHECK(resampled.insert(2, 2, ze(2, 2)).has_value());  // breaks shared support

    auto fast = volume_between(design, shared);
    auto general = volume_between(design, resampled);
    CHECK(fast.has_value());
    CHECK(general.has_value());
    CHECK(fast->cut > 0.0);
    CHECK(fast->fill > 0.0);
    CHECK(std::fabs(fast->net()) < 1e-12);
    const double tol = 1e-9 * std::max(1.0, fast->cut + fast->fill);
    CHECK(std::fabs(fast->cut - general->cut) < tol);
    CHECK(std::fabs(fast->fill - general->fill) < tol);
    CHECK(std::fabs(fast->area - general->area) < tol);
    CHECK(std::fabs(fast->area - 16.0) < 1e-12);
}

// ---- test-side reference geometry for the tiling audit (plain double; independent of
// the engine implementation) -------------------------------------------------------------

struct P2 {
    double x;
    double y;
};

double poly_area(const std::vector<P2>& p) {
    double a = 0.0;
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const P2& s = p[i];
        const P2& e = p[(i + 1) % n];
        a += s.x * e.y - s.y * e.x;
    }
    return 0.5 * std::fabs(a);
}

// Andrew monotone chain; returns the hull CCW.
std::vector<P2> convex_hull(std::vector<P2> pts) {
    std::sort(pts.begin(), pts.end(),
              [](const P2& a, const P2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    auto cross = [](const P2& o, const P2& a, const P2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<P2> h(2 * pts.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0.0) --k;
        h[k++] = pts[i];
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = pts.size() - 1; i > 0; --i) {
        while (k >= lower && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0.0) --k;
        h[k++] = pts[i - 1];
    }
    h.resize(k - 1);
    return h;
}

// Sutherland-Hodgman of `subject` against the convex CCW polygon `clip`.
std::vector<P2> clip_convex(std::vector<P2> subject, const std::vector<P2>& clip) {
    for (std::size_t e = 0; e < clip.size() && subject.size() >= 3; ++e) {
        const P2 a = clip[e];
        const P2 b = clip[(e + 1) % clip.size()];
        std::vector<P2> out;
        for (std::size_t i = 0; i < subject.size(); ++i) {
            const P2 s = subject[i];
            const P2 t = subject[(i + 1) % subject.size()];
            const double ds = (b.x - a.x) * (s.y - a.y) - (b.y - a.y) * (s.x - a.x);
            const double dt = (b.x - a.x) * (t.y - a.y) - (b.y - a.y) * (t.x - a.x);
            if (ds >= 0.0) out.push_back(s);
            if ((ds > 0.0 && dt < 0.0) || (ds < 0.0 && dt > 0.0)) {
                const double u = ds / (ds - dt);
                out.push_back(P2{s.x + u * (t.x - s.x), s.y + u * (t.y - s.y)});
            }
        }
        subject = std::move(out);
    }
    if (subject.size() < 3) subject.clear();
    return subject;
}

void test_tiling_audit_randomized() {
    // Seeded pseudo-random configuration: for every triangle tA of the design TIN, the
    // overlap area reported by the overlay (probed through a single-triangle design TIN)
    // must equal the independently computed area of tA intersected with hull(existing):
    // the clipped pieces tile tA's overlap exactly (no gap, no double count). The
    // per-triangle pieces must also reassemble into the full result, and the full overlap
    // area must equal the area of hull(design) intersected with hull(existing).
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    auto rnd = [&state]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>((state >> 11) & 0xFFFFFFull) / 16777216.0;
    };
    Tin a;
    Tin b;
    for (int i = 0; i < 24; ++i) {
        CHECK(a.insert(10.0 * rnd(), 10.0 * rnd(), 5.0 * rnd()).has_value());
    }
    for (int i = 0; i < 24; ++i) {
        CHECK(b.insert(4.0 + 10.0 * rnd(), 10.0 * rnd(), 5.0 * rnd()).has_value());
    }

    std::vector<P2> bxy;
    for (std::size_t i = 0; i < b.vertex_count(); ++i) {
        const TinVertex& v = b.vertex(static_cast<VertexId>(i));
        bxy.push_back(P2{v.x, v.y});
    }
    const std::vector<P2> hull_b = convex_hull(bxy);

    auto full = volume_between(a, b);
    CHECK(full.has_value());
    CHECK(full->area > 0.0);

    double sum_area = 0.0;
    double sum_cut = 0.0;
    double sum_fill = 0.0;
    for (const auto& tri : a.triangles()) {
        Tin single;
        std::vector<P2> txy;
        for (VertexId v : tri) {
            const TinVertex& p = a.vertex(v);
            CHECK(single.insert(p.x, p.y, p.z).has_value());
            txy.push_back(P2{p.x, p.y});
        }
        auto piece = volume_between(single, b);
        CHECK(piece.has_value());
        const double expect = poly_area(clip_convex(txy, hull_b));
        CHECK(std::fabs(piece->area - expect) < 1e-9 * std::max(1.0, expect));
        sum_area += piece->area;
        sum_cut += piece->cut;
        sum_fill += piece->fill;
    }
    CHECK(std::fabs(sum_area - full->area) < 1e-9 * std::max(1.0, full->area));
    CHECK(std::fabs(sum_cut - full->cut) < 1e-9 * std::max(1.0, full->cut));
    CHECK(std::fabs(sum_fill - full->fill) < 1e-9 * std::max(1.0, full->fill));

    std::vector<P2> axy;
    for (std::size_t i = 0; i < a.vertex_count(); ++i) {
        const TinVertex& v = a.vertex(static_cast<VertexId>(i));
        axy.push_back(P2{v.x, v.y});
    }
    const double hull_overlap = poly_area(clip_convex(convex_hull(axy), hull_b));
    CHECK(std::fabs(full->area - hull_overlap) < 1e-9 * std::max(1.0, hull_overlap));
}

void test_degenerate_touching_is_exactly_zero() {
    // Hulls touching along a shared boundary edge: the overlap is a segment, so the
    // result must be EXACT zeros (pairs skipped via exact orientation tests), not small
    // doubles.
    Tin a = flat_square(2, 0.0);
    Tin b;
    CHECK(b.insert(2, 0, 5).has_value());
    CHECK(b.insert(4, 0, 5).has_value());
    CHECK(b.insert(4, 2, 5).has_value());
    CHECK(b.insert(2, 2, 5).has_value());
    auto r = volume_between(a, b);
    CHECK(r.has_value());
    CHECK(r->cut == 0.0);
    CHECK(r->fill == 0.0);
    CHECK(r->area == 0.0);

    // Vertex-on-edge touch from outside: hull(b2) meets the square only at (2,1), the
    // interior of its right edge. Point overlap => exact zeros.
    Tin b2;
    CHECK(b2.insert(2, 1, 5).has_value());
    CHECK(b2.insert(4, 0, 5).has_value());
    CHECK(b2.insert(4, 2, 5).has_value());
    auto r2 = volume_between(a, b2);
    CHECK(r2.has_value());
    CHECK(r2->cut == 0.0);
    CHECK(r2->fill == 0.0);
    CHECK(r2->area == 0.0);
}

void test_degenerate_vertex_on_edge_no_double_count() {
    // existing has its centre vertex exactly ON design's interior diagonal (the centre
    // (1,1) lies on either diagonal of the square): collinear / touching boundaries
    // inside the overlap must not double-count. Flat vs flat => exactly area * dz.
    Tin design = flat_square(2, 0.0);
    Tin existing;
    CHECK(existing.insert(0, 0, 1).has_value());
    CHECK(existing.insert(2, 0, 1).has_value());
    CHECK(existing.insert(2, 2, 1).has_value());
    CHECK(existing.insert(0, 2, 1).has_value());
    CHECK(existing.insert(1, 1, 1).has_value());
    auto r = volume_between(design, existing);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 4.0) < 1e-12);
    CHECK(r->fill == 0.0);
    CHECK(std::fabs(r->area - 4.0) < 1e-12);
}

void test_prismoidal_vs_analytic() {
    // Pyramid lying on its side: cross-section area A(x) = x^2 over x in [0, 6];
    // V = integral = 72 exactly. Prismoidal is exact for quadratic area variation:
    // L/6 (A1 + 4 Am + A2) = 6/6 (0 + 4*9 + 36) = 72.
    CHECK(prismoidal_volume(0.0, 9.0, 36.0, 6.0) == 72.0);
    // Average end area (labeled report option) overestimates the same solid: 108.
    CHECK(average_end_area_volume(0.0, 36.0, 6.0) == 108.0);
    // Constant cross-section (a prism): both methods agree exactly.
    CHECK(prismoidal_volume(5.0, 5.0, 5.0, 2.0) == 10.0);
    CHECK(average_end_area_volume(5.0, 5.0, 2.0) == 10.0);
}

void test_oracle_cross_check() {
    std::ifstream pin(g_points_path);
    CHECK(static_cast<bool>(pin));
    std::vector<std::array<double, 3>> points;
    std::string line;
    while (std::getline(pin, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "points") {
            std::size_t n = 0;
            ss >> n;
            points.resize(n);
            for (auto& p : points) {
                std::string hx, hy, hz;
                pin >> hx >> hy >> hz;
                p = {hexd(hx), hexd(hy), hexd(hz)};
            }
            break;
        }
    }
    CHECK_EQ(points.size(), static_cast<std::size_t>(500));

    double plane_z = 0.0, oracle_cut = 0.0, oracle_fill = 0.0;
    bool found = false;
    std::ifstream in(g_cv_path);
    CHECK(static_cast<bool>(in));
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "volume_plane") {
            std::string hz, hc, hf;
            ss >> hz >> hc >> hf;
            plane_z = hexd(hz);
            oracle_cut = hexd(hc);
            oracle_fill = hexd(hf);
            found = true;
        }
    }
    CHECK(found);

    Tin tin;
    for (const auto& p : points) CHECK(tin.insert(p[0], p[1], p[2]).has_value());
    auto r = volume_to_plane(tin, plane_z);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - oracle_cut) < 1e-1);  // volume_m3 tolerance
    CHECK(std::fabs(r->fill - oracle_fill) < 1e-1);
    std::printf(
        "volume oracle cross-check: plane %.1f cut %.4f (oracle %.4f) fill %.4f "
        "(oracle %.4f)\n",
        plane_z, r->cut, oracle_cut, r->fill, oracle_fill);
}

void run() {
    test_tetra_prism();
    test_pyramid_prism_and_mixed_split();
    test_sign_convention();
    test_surface_to_surface_shared_support();
    test_mixed_triangle_exact_split();
    test_independent_flat_planes();
    test_independent_tilted_vs_flat();
    test_independent_crossing_surfaces();
    test_independent_partial_overlap();
    test_general_path_matches_shared_support();
    test_tiling_audit_randomized();
    test_degenerate_touching_is_exactly_zero();
    test_degenerate_vertex_on_edge_no_double_count();
    test_prismoidal_vs_analytic();
    test_oracle_cross_check();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <tin_fixture> <cv_fixture>\n", argv[0]);
        return 2;
    }
    g_points_path = argv[1];
    g_cv_path = argv[2];
    run();
    return ::ingeneer::geom::test::g_failures == 0 ? 0 : 1;
}
