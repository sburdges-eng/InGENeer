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
    CoordinateOutOfDomain,   // |coordinate| > kCoordinateLimit (predicate safety domain)
    WalkOverflow,            // point location exceeded the step budget (internal guard)
    BreaklineTooShort,       // breakline resolves to fewer than 2 distinct vertices
    MeshNotInitialized,      // breakline requires at least one finite triangle
    ConstraintIntersection,  // crossing breaklines under CrossingPolicy::Reject (H-6)
};

// Coordinate domain (Phase 6.5, fuzz-found). The exact predicates guarantee correct
// signs only in the absence of overflow AND underflow (see ingeneer/geom/predicates.h):
// the incircle determinant is degree 4 in coordinate differences, so 1e127-scale input
// overflows the filter to +-inf and certifies garbage signs, while 1e-97-scale input
// underflows the exact expansion arithmetic to subnormals and silently corrupts the
// topology (both observed under fuzz). The TIN therefore accepts a coordinate v only
// when v == 0 or kCoordinateMin <= |v| <= kCoordinateLimit, rejecting loudly with
// CoordinateOutOfDomain otherwise.
//   * Upper bound 1e12 m covers every terrestrial CRS (geocentric meters are ~1e7) with
//     > 250 powers of two of headroom before degree-4 overflow.
//   * Lower bound: two DISTINCT doubles of magnitude >= 1e-30 differ by at least
//     ulp(1e-30) ~ 1e-46, so every component of the exact degree-4 expansions stays in
//     the normal range (>= ~1e-248). Nonzero sub-1e-30-meter coordinates are not survey
//     data; exact zero is always allowed.
inline constexpr double kCoordinateLimit = 1e12;
inline constexpr double kCoordinateMin = 1e-30;

// True iff v is an accepted TIN coordinate (see the domain rationale above). NaN and
// +-inf are rejected too (insert reports those as NonFiniteCoordinate first).
inline constexpr bool coordinate_in_domain(double v) noexcept {
    const double a = v < 0.0 ? -v : v;
    return a == 0.0 || (a >= kCoordinateMin && a <= kCoordinateLimit);
}

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
    // present vertex — z is then ignored). Rejects non-finite coordinates
    // (NonFiniteCoordinate) and coordinates outside the exact-predicate safety domain
    // (CoordinateOutOfDomain; see kCoordinateLimit). An accepted point inside the closed
    // convex hull ALWAYS inserts successfully (Phase 6.5; fuzz-verified).
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

    // KERNEL_DEBUG_ASSERT-tier full-mesh audit (plan §3.4; Phase 6.5). Verifies, with
    // exact predicates only: CCW finite triangles, neighbor symmetry, edge valence <= 2,
    // Euler T = 2n - 2 - h, every constrained edge present in the live mesh, and the
    // local constrained-Delaunay property (each interior non-constrained edge passes the
    // two-apex incircle test; constrained edges are excluded — by the Delaunay lemma the
    // local property implies the global one). Aborts on any violation: a corrupt TIN
    // must never reach a caller. Cost O(T) predicate calls per invocation.
    //
    // The function is always compiled (tests and fuzzers call it explicitly); the engine
    // additionally calls it automatically after every mutating public operation when
    // INGENEER_KERNEL_DEBUG_AUDIT is defined (CMake: Debug configurations — the dev,
    // asan-ubsan and tsan presets; absent in hardened/release).
    void debug_audit() const noexcept;

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

    // Scratch for cavity_insert's in-cavity marks, epoch-stamped so a fresh insert costs
    // O(cavity) instead of O(#triangles) re-initialization (measured: the per-call
    // std::vector<bool> made bulk insertion quadratic).
    std::vector<std::uint32_t> mark_;
    std::uint32_t mark_epoch_ = 0;

    TriId alloc_tri();
    void release_tri(TriId t);
    geom::predicates::Orientation orient_vv(VertexId a, VertexId b, double px,
                                            double py) const noexcept;
    bool conflicts(const Tri& t, double px, double py) const noexcept;
    // Exact exhaustive seed: the finite triangle CONTAINING p (always a valid cavity
    // seed — it sits on p's side of every constraint barrier; an arbitrary in-conflict
    // triangle does not, which was the Phase 6.5 fuzz-found insert-rejection wart), else
    // a conflicting ghost (p outside the hull). Reports an exact-duplicate vertex in dup.
    TriId exhaustive_seed(double px, double py, VertexId& dup) const noexcept;
    // Locate a triangle in conflict with p (or detect a duplicate vertex, returned in
    // dup). Returns kNoTriangle only if even the exhaustive fallback finds no conflict.
    TriId locate(double px, double py, VertexId& dup) const noexcept;
    void try_bootstrap() noexcept;
    bool cavity_insert(VertexId p, TriId seed) noexcept;
    // Classical split + Lawson-flip CDT insertion (constrained edges never flipped).
    // Fallback for cavities the barrier-BFS cannot shape into a valid star (possible
    // only near constraints); always succeeds for p inside the hull. Fails (before any
    // mutation) only if `container` does not actually contain p.
    bool insert_split_flip(VertexId p, TriId container) noexcept;
    // 2->4 split of `container`'s edge opposite v[ia] at the already-allocated vertex p
    // (handles a ghost mate: hull edge split). Appends the new finite p-triangles to
    // `suspects` for a subsequent lawson_restore.
    void split_edge(TriId container, std::size_t ia, VertexId p, std::vector<TriId>& suspects);
    // Lawson flips restoring the constrained-Delaunay property around freshly inserted
    // p (constrained and hull edges never flipped; terminates in < vertex_count flips).
    void lawson_restore(VertexId p, std::vector<TriId> suspects) noexcept;
    // Split the CONSTRAINED edge (c, d) at the crossing point (x, y, z) — the classical
    // segment-split primitive (H-6 Split policy). The point is SEMANTICALLY on (c, d)
    // even when its rounded coordinates are a hair off it: both adjacent triangles are
    // 2->4 split, the halves (c, p) / (p, d) are constrained immediately, and Lawson
    // flips restore the CDT on both sides. Fails (before any mutation) with
    // ConstraintIntersection if the rounded point is not strictly inside the adjacent
    // quad (numerically unsplittable; loud per the audited policy).
    std::expected<VertexId, TinError> split_constraint(VertexId c, VertexId d, double x, double y,
                                                       double z) noexcept;
    void replace_neighbor(TriId t, TriId from, TriId to) noexcept;

    // --- breakline machinery (Phase 6.2; src/tin_breakline.cpp) -------------------------
    static std::uint64_t edge_key(VertexId a, VertexId b) noexcept;
    std::expected<BreaklineId, TinError> breakline_impl(std::span<const TinVertex> polyline,
                                                        CrossingPolicy policy);
    // Recover the exact segment a -> b as (a chain of) constrained edges; splits at
    // exactly-hit vertices and applies `policy` at constrained-edge crossings.
    std::expected<void, TinError> recover_edges(VertexId a, VertexId b, CrossingPolicy policy);
    TriId tri_with_vertex(VertexId v) const noexcept;
    // Replace `region` (the triangles crossed by segment a -> b) with the Anglada
    // pseudo-polygon retriangulation of the left/right boundary chains. Returns false —
    // BEFORE any mutation — if the retriangulation does not close up exactly (every
    // chain vertex consumed; every new edge paired with exactly one other new edge or
    // exactly one region-boundary edge). Unreachable with a correct incircle (Anglada's
    // theorem), but geometry_core's incircle_2d filter defect (see debug_audit) can
    // select a wrong Delaunay mate and leave a hole; callers roll the breakline back.
    [[nodiscard]] bool retriangulate_region(const std::vector<TriId>& region, VertexId a,
                                            VertexId b, const std::vector<VertexId>& left,
                                            const std::vector<VertexId>& right);
    void anglada_fill(VertexId u, VertexId w, std::span<const VertexId> chain,
                      std::vector<std::array<VertexId, 3>>& out) const;
};

}  // namespace ingeneer::surface

#endif  // INGENEER_SURFACE_TIN_H
