// surface_core TIN — bulk insertion (T11 BRIO/Hilbert insert_many).
//
// insert_many is REQUIRED to be semantically a reordered sequence of insert() calls, so:
//   * on general-position clouds the Delaunay triangulation is unique and the bulk path
//     must produce the EXACT same canonical triangle set as sequential insertion;
//   * on tie-heavy (cocircular) clouds only structural equivalence is order-independent,
//     so those cases are held to the full debug_audit() + Euler invariants instead;
//   * the id vector maps 1:1 to input order, duplicates resolving to the existing id;
//   * the batch is all-or-nothing: any invalid point leaves the TIN untouched.
#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include "check.hpp"
#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;

namespace {

// Same fixed-seed LCG as bench_tin.cpp (deterministic, C-4.6).
struct Lcg {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 33);
    }
    double coord(double scale) { return (static_cast<double>(next()) / 4294967296.0) * scale; }
};

using Pt = std::array<double, 2>;
using CoordTri = std::array<Pt, 3>;

// Canonical triangle set by COORDINATES (vertex ids differ between insertion orders):
// rotate each CCW triple so its lexicographically smallest vertex comes first (cyclic
// order preserved), then sort the list.
std::vector<CoordTri> canon_tris(const Tin& tin) {
    std::vector<CoordTri> out;
    for (const auto& t : tin.triangles()) {
        CoordTri c;
        for (std::size_t i = 0; i < 3; ++i) {
            c[i] = Pt{tin.vertex(t[i]).x, tin.vertex(t[i]).y};
        }
        std::size_t s = 0;
        for (std::size_t i = 1; i < 3; ++i) {
            if (c[i] < c[s]) s = i;
        }
        out.push_back(CoordTri{c[s], c[(s + 1) % 3], c[(s + 2) % 3]});
    }
    std::sort(out.begin(), out.end());
    return out;
}

VertexId find_vertex(const Tin& tin, double x, double y) {
    for (VertexId v = 0; v < tin.vertex_count(); ++v) {
        if (tin.vertex(v).x == x && tin.vertex(v).y == y) return v;
    }
    return kGhostVertex;
}

void run() {
    // --- equivalence vs sequential insert on a general-position fixed cloud -------------
    {
        Lcg rng;
        std::vector<TinVertex> pts(500);
        for (auto& p : pts) p = {rng.coord(10000.0), rng.coord(10000.0), rng.coord(100.0)};

        Tin seq;
        for (const auto& p : pts) CHECK(seq.insert(p.x, p.y, p.z).has_value());

        Tin bulk;
        const auto ids = bulk.insert_many(pts);
        CHECK(ids.has_value());
        CHECK_EQ(ids->size(), pts.size());
        CHECK_EQ(bulk.vertex_count(), seq.vertex_count());
        CHECK_EQ(bulk.triangle_count(), seq.triangle_count());
        CHECK_EQ(bulk.hull_size(), seq.hull_size());
        CHECK(canon_tris(bulk) == canon_tris(seq));
        bulk.debug_audit();

        // ids map 1:1 to input order (xy AND z: all points distinct in this cloud).
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const TinVertex& v = bulk.vertex((*ids)[i]);
            CHECK(v.x == pts[i].x && v.y == pts[i].y && v.z == pts[i].z);
        }
    }

    // --- tie-heavy lattice (cocircular everywhere): structural + Delaunay audits --------
    {
        constexpr int kSide = 30;
        Lcg rng;
        std::vector<TinVertex> pts;
        pts.reserve(kSide * kSide);
        for (int x = 0; x < kSide; ++x) {
            for (int y = 0; y < kSide; ++y) {
                pts.push_back(
                    {static_cast<double>(x), static_cast<double>(y), static_cast<double>(x + y)});
            }
        }
        for (std::size_t i = pts.size(); i > 1; --i) {  // deterministic Fisher-Yates
            std::swap(pts[i - 1], pts[rng.next() % i]);
        }
        Tin tin;
        const auto ids = tin.insert_many(pts);
        CHECK(ids.has_value());
        CHECK_EQ(tin.vertex_count(), pts.size());
        // Euler invariant T = 2n - 2 - h; lattice hull is the 4 x (kSide-1) boundary.
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(4 * (kSide - 1)));
        CHECK_EQ(tin.triangle_count(), 2 * pts.size() - 2 - tin.hull_size());
        tin.debug_audit();  // CCW, symmetry, valence, Euler, local CDT property
    }

    // --- duplicates in the batch: same id, both against the batch and the TIN -----------
    {
        Tin tin;
        CHECK(tin.insert(0, 0, 7).has_value());
        const VertexId pre = find_vertex(tin, 0, 0);
        const std::vector<TinVertex> pts{
            {5, 5, 1}, {0, 0, 99}, {9, 2, 2}, {5, 5, 42}, {2, 8, 3},
        };
        const auto ids = tin.insert_many(pts);
        CHECK(ids.has_value());
        CHECK_EQ((*ids)[1], pre);          // duplicate of a PRE-EXISTING vertex
        CHECK_EQ((*ids)[3], (*ids)[0]);    // duplicate WITHIN the batch
        CHECK_EQ(tin.vertex(pre).z, 7.0);  // duplicate z ignored (engine-wide rule)
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(4));  // 1 pre + 3 unique new
        tin.debug_audit();
    }

    // --- all-or-nothing: invalid points reject the WHOLE batch, TIN untouched -----------
    {
        Tin tin;
        CHECK(tin.insert(0, 0, 0).has_value());
        CHECK(tin.insert(4, 0, 0).has_value());
        CHECK(tin.insert(2, 3, 0).has_value());
        const auto before = canon_tris(tin);

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const std::vector<TinVertex> bad_nan{{1, 1, 1}, {2, nan, 0}, {3, 1, 1}};
        const auto r1 = tin.insert_many(bad_nan);
        CHECK(!r1.has_value());
        CHECK(r1.error().code == TinErrc::NonFiniteCoordinate);

        const std::vector<TinVertex> bad_dom{{1, 1, 1}, {1e13, 0, 0}};  // > kCoordinateLimit
        const auto r2 = tin.insert_many(bad_dom);
        CHECK(!r2.has_value());
        CHECK(r2.error().code == TinErrc::CoordinateOutOfDomain);

        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(3));
        CHECK(canon_tris(tin) == before);
        tin.debug_audit();
    }

    // --- batch into an EXISTING TIN with constraints ------------------------------------
    {
        Tin tin;
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                CHECK(tin.insert(x, y, x + y).has_value());
            }
        }
        // Diagonal breakline across the grid (not a natural lattice Delaunay diagonal).
        const std::array<TinVertex, 2> bl{TinVertex{0, 0, 0}, TinVertex{4, 4, 8}};
        CHECK(tin.insert_breakline(bl, CrossingPolicy::Reject).has_value());
        const VertexId va = find_vertex(tin, 0, 0);
        const VertexId vb = find_vertex(tin, 4, 4);
        // The segment passes exactly through the lattice diagonal vertices: constrained
        // as a chain (1,1)-(2,2)-(3,3).
        const VertexId v11 = find_vertex(tin, 1, 1);
        const VertexId v22 = find_vertex(tin, 2, 2);
        CHECK(tin.is_constrained(v11, v22));

        // Batch: off-constraint points AND one point exactly ON the constrained edge
        // (1,1)-(2,2): insert() splits the constraint there and BOTH halves must stay
        // constrained through the bulk path.
        const std::vector<TinVertex> batch{
            {0.5, 3.5, 1}, {3.2, 0.7, 2}, {1.5, 1.5, 3}, {3.7, 2.9, 4}, {0.3, 0.9, 5},
        };
        const auto ids = tin.insert_many(batch);
        CHECK(ids.has_value());
        const VertexId mid = (*ids)[2];
        CHECK(!tin.is_constrained(v11, v22));  // split point lands in the edge interior
        CHECK(tin.is_constrained(v11, mid));
        CHECK(tin.is_constrained(mid, v22));
        CHECK(tin.is_constrained(va, v11));  // rest of the chain untouched
        CHECK(tin.is_constrained(find_vertex(tin, 3, 3), vb));
        tin.debug_audit();
    }

    // --- empty / 1-point / collinear batches ---------------------------------------------
    {
        Tin tin;
        const auto empty = tin.insert_many({});
        CHECK(empty.has_value());
        CHECK(empty->empty());
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(0));

        const std::vector<TinVertex> one{{1, 2, 3}};
        const auto ids1 = tin.insert_many(one);
        CHECK(ids1.has_value());
        CHECK_EQ(ids1->size(), static_cast<std::size_t>(1));
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(1));
        CHECK_EQ(tin.vertex((*ids1)[0]).z, 3.0);

        Tin col;
        std::vector<TinVertex> line;
        for (int i = 0; i < 8; ++i) line.push_back({static_cast<double>(i), 2.0 * i, 0});
        const auto idsc = col.insert_many(line);
        CHECK(idsc.has_value());
        CHECK_EQ(col.vertex_count(), static_cast<std::size_t>(8));
        CHECK_EQ(col.triangle_count(), static_cast<std::size_t>(0));
        // Collinear bootstrap buffer must resolve once a non-collinear point arrives.
        CHECK(col.insert(3, 100, 1).has_value());
        CHECK(col.triangle_count() > 0);
        col.debug_audit();
    }
}

}  // namespace

TEST_MAIN_RUN()
