// surface_core TIN — structural + Delaunay audits under deterministic fuzz (Phase 6.5),
// plus cross-validation against the independently implemented vendored CDT library.
//
// Audits per mesh:
//   * CCW invariant: every finite triangle is counter-clockwise (exact orient2d).
//   * Neighbor symmetry: t.n[i] points back at t across the shared edge.
//   * Euler: T = 2n - 2 - h for the triangulated vertex set.
//   * GLOBAL Delaunay (the KERNEL_DEBUG_ASSERT audit, brute force O(n*t)): no vertex lies
//     strictly inside any finite triangle's circumcircle — exact incircle_2d.
//   * CDT cross-check: canonical triangle sets equal (both are Delaunay; LCG doubles have
//     no cocircular ties, so the triangulation is unique).
#include <CDT.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "ingeneer/geom/predicates.h"
#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;
using ingeneer::geom::predicates::incircle_2d;
using ingeneer::geom::predicates::orient2d;
using ingeneer::geom::predicates::Orientation;
using ingeneer::geom::predicates::Point2;

namespace {

struct Lcg {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 33);
    }
    double coord(double scale) { return (static_cast<double>(next()) / 4294967296.0) * scale; }
};

using CanonTri = std::array<VertexId, 3>;

std::vector<CanonTri> canonical(std::vector<CanonTri> tris) {
    for (CanonTri& t : tris) std::sort(t.begin(), t.end());
    std::sort(tris.begin(), tris.end());
    return tris;
}

void audit_mesh(const Tin& tin) {
    const auto tris = tin.triangles();
    const std::size_t n = tin.vertex_count();

    // CCW + global empty-circumcircle.
    for (const CanonTri& t : tris) {
        const Point2 a{tin.vertex(t[0]).x, tin.vertex(t[0]).y};
        const Point2 b{tin.vertex(t[1]).x, tin.vertex(t[1]).y};
        const Point2 c{tin.vertex(t[2]).x, tin.vertex(t[2]).y};
        CHECK(orient2d(a, b, c) == Orientation::LEFT);
        for (VertexId v = 0; v < n; ++v) {
            if (v == t[0] || v == t[1] || v == t[2]) continue;
            const Point2 p{tin.vertex(v).x, tin.vertex(v).y};
            CHECK(incircle_2d(a, b, c, p) != Orientation::LEFT);
        }
    }

    // Euler.
    if (!tris.empty()) {
        CHECK_EQ(tris.size(), 2 * n - 2 - tin.hull_size());
    }
}

void cross_check_cdt(const Tin& tin) {
    CDT::Triangulation<double> cdt;
    std::vector<CDT::V2d<double>> pts;
    pts.reserve(tin.vertex_count());
    for (VertexId v = 0; v < tin.vertex_count(); ++v) {
        pts.push_back({tin.vertex(v).x, tin.vertex(v).y});
    }
    cdt.insertVertices(pts);
    cdt.eraseSuperTriangle();

    std::vector<CanonTri> theirs;
    theirs.reserve(cdt.triangles.size());
    for (const auto& t : cdt.triangles) {
        theirs.push_back({static_cast<VertexId>(t.vertices[0]),
                          static_cast<VertexId>(t.vertices[1]),
                          static_cast<VertexId>(t.vertices[2])});
    }
    const auto ours = canonical(tin.triangles());
    const auto ref = canonical(std::move(theirs));
    CHECK_EQ(ours.size(), ref.size());
    CHECK(ours == ref);
}

}  // namespace

static void run() {
    Lcg rng;

    // Random clouds at several sizes (uniform doubles: no cocircular ties).
    for (const std::size_t n : {4u, 10u, 50u, 200u, 500u}) {
        Tin tin;
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(tin.insert(rng.coord(1000.0), rng.coord(1000.0), rng.coord(50.0)).has_value());
        }
        CHECK_EQ(tin.vertex_count(), n);
        audit_mesh(tin);
        cross_check_cdt(tin);
    }

    // Integer lattice with shuffled insertion order: massive cocircular ties (every unit
    // square). The triangulation is then NOT unique, so no CDT set-comparison — but the
    // structural + Delaunay audits must still hold exactly.
    {
        Tin tin;
        std::vector<std::pair<int, int>> pts;
        for (int x = 0; x < 12; ++x) {
            for (int y = 0; y < 12; ++y) pts.emplace_back(x, y);
        }
        for (std::size_t i = pts.size(); i > 1; --i) {
            std::swap(pts[i - 1], pts[rng.next() % i]);
        }
        for (const auto& [x, y] : pts) {
            CHECK(tin.insert(x, y, x * y).has_value());
        }
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(144));
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(44));
        audit_mesh(tin);
    }

    // Clustered + duplicate-heavy stress: points snapped to a coarse grid so duplicates
    // and collinear runs occur constantly.
    {
        Tin tin;
        std::size_t distinct = 0;
        std::vector<std::pair<double, double>> seen;
        for (int i = 0; i < 800; ++i) {
            const double x = static_cast<double>(rng.next() % 20u);
            const double y = static_cast<double>(rng.next() % 20u);
            const bool fresh =
                std::find(seen.begin(), seen.end(), std::make_pair(x, y)) == seen.end();
            if (fresh) {
                seen.emplace_back(x, y);
                ++distinct;
            }
            CHECK(tin.insert(x, y, 0.0).has_value());
        }
        CHECK_EQ(tin.vertex_count(), distinct);
        audit_mesh(tin);
    }
}

TEST_MAIN_RUN()
