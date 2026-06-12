// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/surface/tin.h — incremental Delaunay TIN on an index-handle mesh (Phase 6.1).
//
// Custom in-house engine per ADR-0012, built exclusively on the exact predicates of
// geometry_core (orient2d / incircle_2d): the engine performs NO floating-point
// comparisons of its own beyond exact coordinate equality for duplicate detection, so its
// topological decisions inherit the predicates' exactness guarantees.
//
// Design:
//   * Index handles only (std::uint32_t) — no pointers in the public surface; mesh
//     storage is two flat vectors (vertices, triangles), trivially serializable.
//   * Ghost triangles: every convex-hull edge is bounded by a "ghost" triangle containing
//     the symbolic infinite vertex (kGhostVertex). Insertion outside the hull is the same
//     cavity operation as insertion inside it (Bowyer–Watson with a unified conflict
//     rule), eliminating the classic finite-super-triangle distortion bug.
//   * Duplicate xy coordinates (exact equality) return the existing vertex id; the new z
//     is ignored. Surveyors resolve duplicate-shot conflicts upstream of the TIN.
//   * Collinear prefixes are buffered until the first non-collinear point arrives;
//     an all-collinear point set has zero finite triangles.
//
// Oracle discipline (ADR-0023): validated against TOTaLi's frozen scipy/Qhull semantics
// via tests/fixtures/oracle/ and cross-checked against the independently implemented
// vendored CDT library.
#ifndef INGENEER_SURFACE_TIN_H
#define INGENEER_SURFACE_TIN_H

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <unordered_set>
#include <vector>

#include "ingeneer/geom/predicates.h"

namespace ingeneer::surface {

using VertexId = std::uint32_t;
using TriId = std::uint32_t;
using BreaklineId = std::uint32_t;

inline constexpr VertexId kGhostVertex = 0xFFFFFFFFu;
inline constexpr TriId kNoTriangle = 0xFFFFFFFFu;

enum class TinErrc {
    NonFiniteCoordinate,     // NaN or +-inf input
    WalkOverflow,            // point location exceeded the step budget (internal guard)
    BreaklineTooShort,       // breakline resolves to fewer than 2 distinct vertices
    MeshNotInitialized,      // breakline requires at least one finite triangle
    ConstraintIntersection,  // crossing breaklines under CrossingPolicy::Reject (H-6)
};

struct TinError {
    TinErrc code;
};

struct TinVertex {
    double x;
    double y;
    double z;
};

// H-6 — crossing-breakline policy. ALWAYS an explicit parameter on the API; the engine has
// no silent default. Crossing-Z disagreement between two breaklines is a surveyor input
// conflict that the engine resolves deterministically per the AUDITED rules below — never
// heuristically and never silently.
//
// Split semantics (the audited policy):
//   * The NEW breakline splits the EXISTING constrained edge: both polylines receive a
//     shared vertex at the (double-precision) segment intersection point, and all four
//     sub-segments become constrained edges. The intersection point's xy is computed in
//     doubles; all crossing CLASSIFICATION is exact (orient2d).
//   * Z-combination rule: z at the intersection vertex is linearly interpolated along the
//     NEW breakline's current working segment — the newest data wins. The existing
//     breakline conforms to that vertex, consistent with the engine-wide rule that an
//     existing vertex's z is never rewritten. (Working segments are the new breakline's
//     sub-segments after splitting at any existing vertices it passes through exactly;
//     such endpoints carry the TIN's vertex z by the duplicate-z rule.)
//   * Degenerate fallback: if the computed intersection collapses exactly onto one of the
//     new segment's endpoints (numerically unsplittable), the crossing is rejected with
//     ConstraintIntersection even under Split — loud failure over a silent hang.
enum class CrossingPolicy : std::uint8_t {
    Reject,  // error ConstraintIntersection; the TIN is rolled back to its pre-call state
    Split,   // split BOTH breaklines at the intersection (see audited Z rule above)
};

class Tin {
public:
    Tin() = default;

    // Insert a point. Returns the vertex id (the EXISTING id if (x, y) exactly matches a
    // present vertex — z is then ignored). Rejects non-finite coordinates.
    //
    // Constrained edges (breaklines) act as barriers: the Bowyer–Watson conflict region
    // never crosses one, so a constrained edge survives later insertions. A point landing
    // EXACTLY on a constrained edge (exact orient2d collinearity, within the segment)
    // splits the constraint at that point: both halves stay constrained.
    std::expected<VertexId, TinError> insert(double x, double y, double z) noexcept;

    // Insert a polyline of constrained edges ("breakline", Phase 6.2). Endpoints are
    // inserted as vertices first (exact-duplicate xy reuses the existing vertex; its z is
    // kept), then each consecutive pair is recovered as a constrained edge via cavity
    // carving + Anglada pseudo-polygon retriangulation. Exact-predicate guarantees:
    //   * a segment passing exactly through an existing vertex splits at that vertex;
    //   * a segment collinear-overlapping an existing constrained edge MERGES with it
    //     (shared sub-edges are re-constrained idempotently — never an error);
    //   * a segment properly crossing an existing constrained edge follows `policy`
    //     (see CrossingPolicy — explicit, audited, never silent).
    // On ANY error the TIN is rolled back to its exact pre-call state.
    std::expected<BreaklineId, TinError> insert_breakline(std::span<const TinVertex> polyline,
                                                          CrossingPolicy policy) noexcept;

    // True iff the undirected edge (a, b) is currently a constrained (breakline) edge.
    bool is_constrained(VertexId a, VertexId b) const noexcept;
    std::size_t constrained_edge_count() const noexcept { return constrained_.size(); }
    std::size_t breakline_count() const noexcept { return breakline_count_; }

    std::size_t vertex_count() const noexcept { return vertices_.size(); }
    const TinVertex& vertex(VertexId v) const noexcept { return vertices_[v]; }

    // Number of finite (non-ghost) triangles.
    std::size_t triangle_count() const noexcept;
    // Number of hull edges (== live ghost triangles).
    std::size_t hull_size() const noexcept;

    // All finite triangles as CCW vertex-id triples (live triangles only, in storage
    // order — canonicalize in the caller if set semantics are needed).
    std::vector<std::array<VertexId, 3>> triangles() const;

private:
    struct Tri {
        std::array<VertexId, 3> v{kGhostVertex, kGhostVertex, kGhostVertex};
        std::array<TriId, 3> n{kNoTriangle, kNoTriangle, kNoTriangle};  // n[i] opposite v[i]
        bool alive = false;
        bool ghost = false;
    };

    std::vector<TinVertex> vertices_;
    std::vector<Tri> tris_;
    std::vector<TriId> free_;        // recycled triangle slots
    std::vector<VertexId> pending_;  // collinear bootstrap buffer (vertex ids)
    bool initialized_ = false;       // first finite triangle exists
    TriId last_tri_ = kNoTriangle;   // walk hint

    // Constrained (breakline) edges as canonical (lo << 32 | hi) vertex-id pairs. Every
    // key refers to an edge that exists in the live mesh (invariant maintained by all
    // mutating operations).
    std::unordered_set<std::uint64_t> constrained_;
    std::uint32_t breakline_count_ = 0;

    TriId alloc_tri();
    void release_tri(TriId t);
    geom::predicates::Orientation orient_vv(VertexId a, VertexId b, double px,
                                            double py) const noexcept;
    bool conflicts(const Tri& t, double px, double py) const noexcept;
    // Locate a triangle in conflict with p (or detect a duplicate vertex, returned in
    // dup). Returns kNoTriangle only if even the exhaustive fallback finds no conflict.
    TriId locate(double px, double py, VertexId& dup) const noexcept;
    void try_bootstrap() noexcept;
    bool cavity_insert(VertexId p, TriId seed) noexcept;

    // --- breakline machinery (Phase 6.2; src/tin_breakline.cpp) -------------------------
    static std::uint64_t edge_key(VertexId a, VertexId b) noexcept;
    std::expected<BreaklineId, TinError> breakline_impl(std::span<const TinVertex> polyline,
                                                        CrossingPolicy policy);
    // Recover the exact segment a -> b as (a chain of) constrained edges; splits at
    // exactly-hit vertices and applies `policy` at constrained-edge crossings.
    std::expected<void, TinError> recover_edges(VertexId a, VertexId b, CrossingPolicy policy);
    TriId tri_with_vertex(VertexId v) const noexcept;
    // Replace `region` (the triangles crossed by segment a -> b) with the Anglada
    // pseudo-polygon retriangulation of the left/right boundary chains.
    void retriangulate_region(const std::vector<TriId>& region, VertexId a, VertexId b,
                              const std::vector<VertexId>& left,
                              const std::vector<VertexId>& right);
    void anglada_fill(VertexId u, VertexId w, std::span<const VertexId> chain,
                      std::vector<std::array<VertexId, 3>>& out) const;
};

}  // namespace ingeneer::surface

#endif  // INGENEER_SURFACE_TIN_H
