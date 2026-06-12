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
#include <vector>

#include "ingeneer/geom/predicates.h"

namespace ingeneer::surface {

using VertexId = std::uint32_t;
using TriId = std::uint32_t;

inline constexpr VertexId kGhostVertex = 0xFFFFFFFFu;
inline constexpr TriId kNoTriangle = 0xFFFFFFFFu;

enum class TinErrc {
    NonFiniteCoordinate,  // NaN or +-inf input
    WalkOverflow,         // point location exceeded the step budget (internal guard)
};

struct TinError {
    TinErrc code;
};

struct TinVertex {
    double x;
    double y;
    double z;
};

class Tin {
public:
    Tin() = default;

    // Insert a point. Returns the vertex id (the EXISTING id if (x, y) exactly matches a
    // present vertex — z is then ignored). Rejects non-finite coordinates.
    std::expected<VertexId, TinError> insert(double x, double y, double z) noexcept;

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
};

}  // namespace ingeneer::surface

#endif  // INGENEER_SURFACE_TIN_H
