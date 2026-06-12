// surface_core TIN — breakline (constrained-edge) insertion + H-6 crossing policy
// (Phase 6.2 TDD).
//
// Audits per mesh (run after EVERY mutating operation):
//   * CCW invariant: every finite triangle is counter-clockwise (exact orient2d).
//   * Euler: T = 2n - 2 - h.
//   * Constrained Delaunay: for every INTERIOR NON-CONSTRAINED edge, neither adjacent
//     apex lies strictly inside the other triangle's circumcircle (exact incircle_2d).
//     Constrained edges are EXCLUDED — they legitimately violate Delaunay.
//   * Edge sanity: no undirected edge has more than two incident finite triangles.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
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

using CanonTri = std::array<VertexId, 3>;

std::vector<CanonTri> canonical(std::vector<CanonTri> tris) {
    for (CanonTri& t : tris) std::sort(t.begin(), t.end());
    std::sort(tris.begin(), tris.end());
    return tris;
}

VertexId find_vertex(const Tin& tin, double x, double y) {
    for (VertexId v = 0; v < tin.vertex_count(); ++v) {
        if (tin.vertex(v).x == x && tin.vertex(v).y == y) return v;
    }
    return kGhostVertex;
}

VertexId find_vertex_near(const Tin& tin, double x, double y, double tol) {
    for (VertexId v = 0; v < tin.vertex_count(); ++v) {
        if (std::fabs(tin.vertex(v).x - x) <= tol && std::fabs(tin.vertex(v).y - y) <= tol) {
            return v;
        }
    }
    return kGhostVertex;
}

bool edge_in_mesh(const Tin& tin, VertexId a, VertexId b) {
    for (const CanonTri& t : tin.triangles()) {
        for (int i = 0; i < 3; ++i) {
            const VertexId u = t[static_cast<std::size_t>(i)];
            const VertexId w = t[static_cast<std::size_t>((i + 1) % 3)];
            if ((u == a && w == b) || (u == b && w == a)) return true;
        }
    }
    return false;
}

Point2 pt(const Tin& tin, VertexId v) { return Point2{tin.vertex(v).x, tin.vertex(v).y}; }

// Structural + constrained-Delaunay audit (see file header).
void audit(const Tin& tin) {
    const auto tris = tin.triangles();
    const std::size_t n = tin.vertex_count();

    for (const CanonTri& t : tris) {
        CHECK(orient2d(pt(tin, t[0]), pt(tin, t[1]), pt(tin, t[2])) == Orientation::LEFT);
    }
    if (!tris.empty()) {
        CHECK_EQ(tris.size(), 2 * n - 2 - tin.hull_size());
    }

    // Interior-edge map: undirected edge -> incident (triangle, apex) pairs.
    struct Incidence {
        CanonTri tri;
        VertexId apex;
    };
    std::unordered_map<std::uint64_t, std::vector<Incidence>> edges;
    for (const CanonTri& t : tris) {
        for (int i = 0; i < 3; ++i) {
            const VertexId u = t[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId w = t[static_cast<std::size_t>((i + 2) % 3)];
            const VertexId lo = u < w ? u : w;
            const VertexId hi = u < w ? w : u;
            const std::uint64_t key = (static_cast<std::uint64_t>(lo) << 32) | hi;
            edges[key].push_back(Incidence{t, t[static_cast<std::size_t>(i)]});
        }
    }
    for (const auto& [key, inc] : edges) {
        CHECK(inc.size() <= 2);
        if (inc.size() != 2) continue;
        const VertexId lo = static_cast<VertexId>(key >> 32);
        const VertexId hi = static_cast<VertexId>(key & 0xFFFFFFFFu);
        if (tin.is_constrained(lo, hi)) continue;  // constrained edges may violate Delaunay
        // The two directional incircle tests are equivalent statements about the same
        // quad; a violation must be confirmed by BOTH. A single-direction disagreement
        // indicates the known geometry_core incircle_2d Layer-A filter defect (wrong
        // sign certified on near-cocircular input; found by the Phase 6.5 audit and
        // reported upstream — geometry_core is read-only for this phase), not a mesh
        // defect. With a correct incircle both directions agree and this check has full
        // strength. Same rule as Tin::debug_audit.
        Orientation dir[2];
        for (int k = 0; k < 2; ++k) {
            const CanonTri& t = inc[static_cast<std::size_t>(k)].tri;
            const VertexId other = inc[static_cast<std::size_t>(1 - k)].apex;
            dir[k] = incircle_2d(pt(tin, t[0]), pt(tin, t[1]), pt(tin, t[2]), pt(tin, other));
        }
        CHECK(dir[0] != Orientation::LEFT || dir[1] != Orientation::LEFT);
    }
}

Tin make_grid(int nx, int ny) {
    Tin tin;
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            CHECK(tin.insert(x, y, x + y).has_value());
        }
    }
    return tin;
}

}  // namespace

static void run() {
    // --- 1. simple breakline across a TIN: new endpoint vertices, edge constrained ------
    {
        Tin tin = make_grid(4, 4);
        const std::size_t n0 = tin.vertex_count();
        const TinVertex bl[] = {{0.2, 0.7, 10.0}, {2.8, 2.3, 20.0}};
        const auto id = tin.insert_breakline(bl, CrossingPolicy::Reject);
        CHECK(id.has_value());
        CHECK_EQ(*id, static_cast<BreaklineId>(0));
        CHECK_EQ(tin.breakline_count(), static_cast<std::size_t>(1));
        CHECK_EQ(tin.vertex_count(), n0 + 2);
        const VertexId va = find_vertex(tin, 0.2, 0.7);
        const VertexId vb = find_vertex(tin, 2.8, 2.3);
        CHECK(va != kGhostVertex);
        CHECK(vb != kGhostVertex);
        CHECK(tin.is_constrained(va, vb));
        CHECK(edge_in_mesh(tin, va, vb));
        CHECK_EQ(tin.constrained_edge_count(), static_cast<std::size_t>(1));
        audit(tin);
    }

    // --- 2. endpoints already existing as vertices (z ignored on duplicates) ------------
    {
        Tin tin = make_grid(4, 4);
        const std::size_t n0 = tin.vertex_count();
        const VertexId va = find_vertex(tin, 0, 0);
        const VertexId vb = find_vertex(tin, 2, 1);
        CHECK(!edge_in_mesh(tin, va, vb));  // not a natural lattice Delaunay edge
        const TinVertex bl[] = {{0.0, 0.0, 99.0}, {2.0, 1.0, 99.0}};
        CHECK(tin.insert_breakline(bl, CrossingPolicy::Reject).has_value());
        CHECK_EQ(tin.vertex_count(), n0);  // both endpoints deduplicated
        CHECK(tin.is_constrained(va, vb));
        CHECK(edge_in_mesh(tin, va, vb));
        CHECK_EQ(tin.vertex(va).z, 0.0);  // duplicate z ignored (engine-wide rule)
        CHECK_EQ(tin.vertex(vb).z, 3.0);
        audit(tin);
    }

    // --- 3. one endpoint coincident with an existing vertex, one fresh ------------------
    {
        Tin tin = make_grid(4, 4);
        const std::size_t n0 = tin.vertex_count();
        const TinVertex bl[] = {{0.4, 2.6, 5.0}, {3.0, 3.0, 77.0}};
        CHECK(tin.insert_breakline(bl, CrossingPolicy::Reject).has_value());
        CHECK_EQ(tin.vertex_count(), n0 + 1);
        const VertexId va = find_vertex(tin, 0.4, 2.6);
        const VertexId vb = find_vertex(tin, 3, 3);
        CHECK(va != kGhostVertex);
        CHECK(vb != kGhostVertex);
        CHECK(tin.is_constrained(va, vb));
        CHECK_EQ(tin.vertex(vb).z, 6.0);  // existing corner z untouched
        audit(tin);
    }

    // --- 4. breakline passing exactly through an existing vertex: split at the vertex ---
    {
        Tin tin = make_grid(4, 4);
        const std::size_t n0 = tin.vertex_count();
        const TinVertex bl[] = {{0.0, 0.0, 0.0}, {2.0, 2.0, 0.0}};  // through (1, 1) exactly
        CHECK(tin.insert_breakline(bl, CrossingPolicy::Reject).has_value());
        CHECK_EQ(tin.vertex_count(), n0);
        const VertexId v00 = find_vertex(tin, 0, 0);
        const VertexId v11 = find_vertex(tin, 1, 1);
        const VertexId v22 = find_vertex(tin, 2, 2);
        CHECK(tin.is_constrained(v00, v11));
        CHECK(tin.is_constrained(v11, v22));
        CHECK(!tin.is_constrained(v00, v22));
        CHECK(edge_in_mesh(tin, v00, v11));
        CHECK(edge_in_mesh(tin, v11, v22));
        audit(tin);
    }

    // --- 5. two crossing breaklines, Reject policy: error, TIN fully rolled back --------
    {
        Tin tin = make_grid(4, 4);
        const TinVertex bl1[] = {{0.3, 0.2, 10.0}, {2.7, 2.2, 20.0}};
        CHECK(tin.insert_breakline(bl1, CrossingPolicy::Reject).has_value());
        audit(tin);

        const auto tris_before = canonical(tin.triangles());
        const std::size_t n_before = tin.vertex_count();
        const std::size_t cec_before = tin.constrained_edge_count();
        CHECK_EQ(cec_before, static_cast<std::size_t>(1));

        const TinVertex bl2[] = {{0.3, 2.2, 40.0}, {2.7, 0.2, 60.0}};  // crosses bl1
        const auto r = tin.insert_breakline(bl2, CrossingPolicy::Reject);
        CHECK(!r.has_value());
        CHECK(r.error().code == TinErrc::ConstraintIntersection);

        CHECK_EQ(tin.vertex_count(), n_before);
        CHECK(canonical(tin.triangles()) == tris_before);
        CHECK_EQ(tin.constrained_edge_count(), cec_before);
        CHECK_EQ(tin.breakline_count(), static_cast<std::size_t>(1));
        audit(tin);
    }

    // --- 6. two crossing breaklines, Split policy: vertex at intersection, lerped z, ----
    // --- all four sub-segments constrained ----------------------------------------------
    {
        Tin tin = make_grid(4, 4);
        const TinVertex bl1[] = {{0.3, 0.2, 10.0}, {2.7, 2.2, 20.0}};
        CHECK(tin.insert_breakline(bl1, CrossingPolicy::Split).has_value());
        const VertexId va0 = find_vertex(tin, 0.3, 0.2);
        const VertexId va1 = find_vertex(tin, 2.7, 2.2);

        const TinVertex bl2[] = {{0.3, 2.2, 40.0}, {2.7, 0.2, 60.0}};
        const auto r = tin.insert_breakline(bl2, CrossingPolicy::Split);
        CHECK(r.has_value());
        CHECK_EQ(tin.breakline_count(), static_cast<std::size_t>(2));

        const VertexId vb0 = find_vertex(tin, 0.3, 2.2);
        const VertexId vb1 = find_vertex(tin, 2.7, 0.2);
        const VertexId vq = find_vertex_near(tin, 1.5, 1.2, 1e-9);  // exact crossing point
        CHECK(vq != kGhostVertex);
        // AUDITED H-6 Z RULE: z at the intersection is lerped along the NEW breakline's
        // working segment: 40 + 0.5 * (60 - 40) = 50 (the new breakline wins).
        CHECK(std::fabs(tin.vertex(vq).z - 50.0) < 1e-6);

        CHECK(tin.is_constrained(va0, vq));
        CHECK(tin.is_constrained(vq, va1));
        CHECK(tin.is_constrained(vb0, vq));
        CHECK(tin.is_constrained(vq, vb1));
        CHECK(!tin.is_constrained(va0, va1));  // the long edge no longer constrained
        CHECK(edge_in_mesh(tin, va0, vq));
        CHECK(edge_in_mesh(tin, vq, va1));
        CHECK(edge_in_mesh(tin, vb0, vq));
        CHECK(edge_in_mesh(tin, vq, vb1));
        CHECK_EQ(tin.constrained_edge_count(), static_cast<std::size_t>(4));
        audit(tin);
    }

    // --- 7. constrained edge survives subsequent nearby insertions ----------------------
    {
        Tin tin = make_grid(4, 4);
        const TinVertex bl[] = {{0.3, 0.2, 10.0}, {2.7, 2.2, 20.0}};
        CHECK(tin.insert_breakline(bl, CrossingPolicy::Reject).has_value());
        const VertexId va = find_vertex(tin, 0.3, 0.2);
        const VertexId vb = find_vertex(tin, 2.7, 2.2);
        CHECK(edge_in_mesh(tin, va, vb));

        // Points hugging the constrained edge on both sides: a pure Delaunay engine would
        // flip the long edge away; the CDT must keep it.
        const double near_pts[][3] = {
            {1.5, 1.25, 7.0}, {1.5, 1.15, 7.0},  {2.0, 1.65, 7.0},
            {1.0, 0.79, 7.0}, {1.45, 1.18, 7.0}, {1.55, 1.22, 7.0},
        };
        for (const auto& p : near_pts) {
            CHECK(tin.insert(p[0], p[1], p[2]).has_value());
            CHECK(tin.is_constrained(va, vb));
            CHECK(edge_in_mesh(tin, va, vb));
            audit(tin);
        }
    }

    // --- 8. point inserted exactly ON a constrained edge splits the constraint ----------
    {
        Tin tin = make_grid(4, 4);
        const TinVertex bl[] = {{0.0, 1.0, 0.0}, {3.0, 1.0, 0.0}};  // along lattice row
        CHECK(tin.insert_breakline(bl, CrossingPolicy::Reject).has_value());
        const VertexId v01 = find_vertex(tin, 0, 1);
        const VertexId v11 = find_vertex(tin, 1, 1);
        CHECK(tin.is_constrained(v01, v11));

        const auto p = tin.insert(0.5, 1.0, 9.0);  // exactly on constrained edge (v01,v11)
        CHECK(p.has_value());
        CHECK(tin.is_constrained(v01, *p));
        CHECK(tin.is_constrained(*p, v11));
        CHECK(!tin.is_constrained(v01, v11));
        CHECK(edge_in_mesh(tin, v01, *p));
        CHECK(edge_in_mesh(tin, *p, v11));
        audit(tin);
    }

    // --- 9. collinear overlap with an existing constrained edge MERGES (idempotent) -----
    {
        Tin tin = make_grid(4, 4);
        const TinVertex bl1[] = {{0.0, 1.0, 0.0}, {3.0, 1.0, 0.0}};  // splits at (1,1),(2,1)
        CHECK(tin.insert_breakline(bl1, CrossingPolicy::Reject).has_value());
        CHECK_EQ(tin.constrained_edge_count(), static_cast<std::size_t>(3));

        const std::size_t n0 = tin.vertex_count();
        const TinVertex bl2[] = {{1.0, 1.0, 5.0}, {3.0, 1.0, 5.0}};  // full overlap subset
        CHECK(tin.insert_breakline(bl2, CrossingPolicy::Reject).has_value());
        CHECK_EQ(tin.constrained_edge_count(), static_cast<std::size_t>(3));  // merged
        CHECK_EQ(tin.vertex_count(), n0);
        audit(tin);
    }

    // --- 10. error cases: short polyline, uninitialized mesh, non-finite, zero length ---
    {
        Tin tin = make_grid(3, 3);
        const TinVertex one[] = {{0.5, 0.5, 0.0}};
        const auto r1 = tin.insert_breakline(one, CrossingPolicy::Reject);
        CHECK(!r1.has_value());
        CHECK(r1.error().code == TinErrc::BreaklineTooShort);

        const std::size_t n0 = tin.vertex_count();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const TinVertex bad[] = {{0.1, 0.1, 0.0}, {nan, 1.0, 0.0}};
        const auto r2 = tin.insert_breakline(bad, CrossingPolicy::Reject);
        CHECK(!r2.has_value());
        CHECK(r2.error().code == TinErrc::NonFiniteCoordinate);
        CHECK_EQ(tin.vertex_count(), n0);  // rollback removed the first inserted endpoint

        // Out of the exact-predicate safety domain (Phase 6.5, fuzz-found): degree-4
        // overflow lets the predicate filters certify garbage signs, and tiny-magnitude
        // coordinates underflow the exact expansions to subnormals. Loud reject; exact
        // zero is always allowed.
        const auto rd = tin.insert(1e300, 0.0, 0.0);
        CHECK(!rd.has_value());
        CHECK(rd.error().code == TinErrc::CoordinateOutOfDomain);
        const auto ru = tin.insert(4e-97, 0.25, 0.0);
        CHECK(!ru.has_value());
        CHECK(ru.error().code == TinErrc::CoordinateOutOfDomain);
        const TinVertex huge[] = {{0.1, 0.1, 0.0}, {2.0, 1e13, 0.0}};
        const auto r2d = tin.insert_breakline(huge, CrossingPolicy::Reject);
        CHECK(!r2d.has_value());
        CHECK(r2d.error().code == TinErrc::CoordinateOutOfDomain);
        CHECK_EQ(tin.vertex_count(), n0);
        CHECK(tin.insert(0.0, 0.25, 0.0).has_value());  // exact zero accepted

        const TinVertex zero[] = {{1.0, 1.0, 0.0}, {1.0, 1.0, 0.0}};  // dedups to one vertex
        const auto r3 = tin.insert_breakline(zero, CrossingPolicy::Reject);
        CHECK(!r3.has_value());
        CHECK(r3.error().code == TinErrc::BreaklineTooShort);
        audit(tin);

        Tin flat;  // all-collinear: never bootstraps
        CHECK(flat.insert(0, 0, 0).has_value());
        CHECK(flat.insert(1, 0, 0).has_value());
        CHECK(flat.insert(2, 0, 0).has_value());
        const TinVertex bl[] = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}};
        const auto r4 = flat.insert_breakline(bl, CrossingPolicy::Reject);
        CHECK(!r4.has_value());
        CHECK(r4.error().code == TinErrc::MeshNotInitialized);
        CHECK_EQ(flat.vertex_count(), static_cast<std::size_t>(3));
    }

    // --- 11. multi-segment polyline + a second non-crossing breakline -------------------
    {
        Tin tin = make_grid(5, 5);
        const TinVertex bl1[] = {{0.3, 0.4, 1.0}, {1.7, 2.3, 2.0}, {3.6, 2.9, 3.0}};
        CHECK(tin.insert_breakline(bl1, CrossingPolicy::Reject).has_value());
        const VertexId a = find_vertex(tin, 0.3, 0.4);
        const VertexId b = find_vertex(tin, 1.7, 2.3);
        const VertexId c = find_vertex(tin, 3.6, 2.9);
        CHECK(tin.is_constrained(a, b));
        CHECK(tin.is_constrained(b, c));
        audit(tin);

        const TinVertex bl2[] = {{0.4, 3.6, 4.0}, {3.5, 3.7, 5.0}};  // disjoint from bl1
        CHECK(tin.insert_breakline(bl2, CrossingPolicy::Reject).has_value());
        CHECK_EQ(tin.breakline_count(), static_cast<std::size_t>(2));
        audit(tin);
    }

    // --- 12. insert microscopically off a post-Split constrained edge SUCCEEDS ----------
    // Fuzz-found minimal repro (originally a fail-loudly regression for the orphan-vertex
    // rollback): the Split-policy crossing creates a ROUNDED intersection vertex q, so
    // (8, 3) — exactly on the original (1,10)-(9,2) constraint line, and exactly the
    // midpoint of the non-constrained mesh edge (9,2)-(7,4) — is microscopically off the
    // post-split constrained edge (q, (9,2)). The visibility walk may cycle here (a CDT
    // is not globally Delaunay), so point location falls back to the exhaustive scan;
    // that scan must return the triangle CONTAINING p (always a valid cavity seed on p's
    // side of every constraint barrier), not merely any in-circumcircle triangle, which
    // can sit across the barrier and make the cavity unrepairable. With the
    // containment-first fallback the insert succeeds and the constraint survives.
    {
        Tin tin;
        CHECK(tin.insert(7.0, 4.0, 0.0).has_value());
        const TinVertex bl1[] = {{5.0, 9.0, 0.0}, {0.0, 3.0, 0.0}};
        CHECK(tin.insert_breakline(bl1, CrossingPolicy::Reject).has_value());
        const TinVertex bl2[] = {{6.0, 9.0, 0.0}, {1.0, 10.0, 0.0}, {9.0, 2.0, 0.0}};
        CHECK(tin.insert_breakline(bl2, CrossingPolicy::Split).has_value());
        audit(tin);

        const std::size_t nverts = tin.vertex_count();
        const std::size_t nconstr = tin.constrained_edge_count();
        const VertexId vq = find_vertex_near(tin, 40.0 / 11.0, 81.0 / 11.0, 1e-9);
        const VertexId v92 = find_vertex(tin, 9, 2);
        CHECK(vq != kGhostVertex);
        CHECK(tin.is_constrained(vq, v92));

        const auto r = tin.insert(8.0, 3.0, 0.0);
        CHECK(r.has_value());
        CHECK_EQ(tin.vertex_count(), nverts + 1);
        // (8, 3) is NOT exactly on the rounded constrained edge (q, (9,2)): the
        // constraint must survive un-split.
        CHECK_EQ(tin.constrained_edge_count(), nconstr);
        CHECK(tin.is_constrained(vq, v92));
        CHECK(edge_in_mesh(tin, vq, v92));
        audit(tin);

        // The TIN remains fully usable after the degenerate insert.
        CHECK(tin.insert(7.5, 6.0, 1.0).has_value());
        audit(tin);
    }

    // --- 13. points exactly ON the original constraint line, post-Split -----------------
    // Same class as 12 swept along the line x + y = 11 (the original (1,10)-(9,2)
    // segment): every inside-hull insert must succeed and never disturb the surviving
    // constraints.
    {
        Tin tin;
        CHECK(tin.insert(7.0, 4.0, 0.0).has_value());
        const TinVertex bl1[] = {{5.0, 9.0, 0.0}, {0.0, 3.0, 0.0}};
        CHECK(tin.insert_breakline(bl1, CrossingPolicy::Reject).has_value());
        const TinVertex bl2[] = {{6.0, 9.0, 0.0}, {1.0, 10.0, 0.0}, {9.0, 2.0, 0.0}};
        CHECK(tin.insert_breakline(bl2, CrossingPolicy::Split).has_value());
        const std::size_t nconstr = tin.constrained_edge_count();
        for (const double x : {4.5, 5.0, 6.0, 6.5, 7.5, 8.5}) {
            CHECK(tin.insert(x, 11.0 - x, 1.0).has_value());
            audit(tin);
        }
        CHECK_EQ(tin.constrained_edge_count(), nconstr);
    }
}

TEST_MAIN_RUN()
