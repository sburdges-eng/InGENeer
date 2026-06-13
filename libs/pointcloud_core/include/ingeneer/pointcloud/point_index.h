// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/point_index.h — in-core nanoflann spatial index (Phase 7).
//
// A nanoflann-backed kd-tree over a PointCloud's SoA-of-points position store, exposing
// k-nearest-neighbour and radius queries. This is the in-CORE index the plan calls for
// (2026-06-11-agentic-work-memory-hardening §4.2(5): "nanoflann L2_Simple_Adaptor over
// SoA float[3]; bulk rebuild, not incremental") and the index the later out-of-core
// octree (2026-06-11-out-of-core-octree-design §6) delegates its per-node k-NN/radius
// work to. This file does NOT implement the octree, LOD, or any on-disk format.
//
// Bulk-rebuild semantics (per the plan): the index is built ONCE over a snapshot of a
// PointCloud and is immutable thereafter. There is no incremental insert. When the cloud
// changes, call build() again (or construct a fresh index) — see rebuild() below. The
// index does not own or copy the cloud's points; it references the cloud's position
// buffer, so the referenced PointCloud must outlive the index and must not be mutated
// while the index is live (mutating the cloud and re-querying without a rebuild is a
// caller error and yields stale results — exactly the bulk-rebuild contract).
//
// Precision: float L2, deliberately. This is the renderer/LOD-facing spatial layer;
// queries are NOT routed through geometry_core's exact double predicates (that is the
// surface/geometry kernel's job and would be both wrong-precision and far too slow for
// millions of points). Distances are squared float L2.
//
// Determinism (CONSTRAINTS C-4.6 / plan H-22): nanoflann's kd-tree build and traversal
// are deterministic functions of the point order and coordinates — no RNG, no
// wall-clock. Identical cloud contents + identical query therefore yield bit-identical
// results across runs and machines (same float arithmetic). See the ordering/tie-break
// notes on each query method.
#ifndef INGENEER_POINTCLOUD_POINT_INDEX_H
#define INGENEER_POINTCLOUD_POINT_INDEX_H

#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

#include "ingeneer/pointcloud/point_cloud.h"

namespace ingeneer::pointcloud {

enum class IndexErrc : std::uint8_t {
    EmptyCloud,       // query issued against an index over a zero-point cloud
    NonFiniteQuery,   // NaN or +-inf in a query coordinate
    InvalidK,         // k == 0 in a knn query
    NegativeRadius,   // radius < 0 in a radius query
    NonFiniteRadius,  // NaN or +-inf radius
    IndexNotBuilt,    // query issued before build()/rebuild()
};

struct IndexError {
    IndexErrc code;
};

// One k-NN result entry: the point's handle and its SQUARED L2 distance to the query.
struct Neighbor {
    PointId id;
    float dist_sq;
};

// A nanoflann kd-tree over a PointCloud. Built by bulk rebuild; immutable until rebuilt.
class SpatialIndex {
public:
    SpatialIndex() noexcept;
    ~SpatialIndex();

    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;
    SpatialIndex(SpatialIndex&&) noexcept;
    SpatialIndex& operator=(SpatialIndex&&) noexcept;

    // Bulk-build the index over `cloud`'s current contents. References (does not copy)
    // the cloud's position buffer: `cloud` must outlive this index and must not be
    // mutated while the index is queried. Re-calling rebuild() discards the previous tree
    // and builds afresh over the cloud's current contents — the ONLY supported way to
    // reflect a mutated cloud (the plan's "bulk rebuild, not incremental"). Building over
    // an empty cloud is allowed (queries then fail with EmptyCloud); this is not an error.
    void rebuild(const PointCloud& cloud);

    // Convenience constructor: build immediately. `cloud` must outlive the index.
    explicit SpatialIndex(const PointCloud& cloud);

    bool is_built() const noexcept { return built_; }
    std::size_t point_count() const noexcept;

    // k-nearest-neighbour query. Returns up to k neighbours (fewer iff the cloud has < k
    // points) SORTED BY ASCENDING squared distance — nanoflann's KNNResultSet guarantees
    // distance-sorted output. Ties at EXACTLY equal squared distance are broken
    // deterministically by nanoflann's fixed traversal order (a pure function of point
    // index order and the kd-tree split structure), so the result is reproducible across
    // runs; it is NOT guaranteed to be ascending-by-PointId among equidistant points.
    // A query coincident with a stored point yields that point first at dist_sq == 0.
    //   * k == 0           -> InvalidK
    //   * empty cloud      -> EmptyCloud
    //   * non-finite query -> NonFiniteQuery
    std::expected<std::vector<Neighbor>, IndexError> knn(float x, float y, float z,
                                                         std::size_t k) const;

    std::expected<std::vector<Neighbor>, IndexError> knn(const Point3f& q, std::size_t k) const {
        return knn(q.x, q.y, q.z, k);
    }

    // Radius query: all points within EUCLIDEAN distance `radius` of the query, i.e. with
    // squared distance <= radius*radius (INCLUSIVE boundary — a point at exactly `radius` is
    // returned, and radius == 0 returns coincident points). Results are SORTED BY ASCENDING
    // squared distance, with equal distances ordered by nanoflann's deterministic (index,
    // distance) comparator — same reproducibility and same non-PointId-only-ordering caveat
    // as knn(). (Implementation note: nanoflann's stock radius set is exclusive on the
    // boundary at both the accept test and the traversal prune; point_index.cpp uses a custom
    // inclusive result set with a one-ULP-widened traversal bound to honour the <= contract.)
    //   * radius < 0       -> NegativeRadius
    //   * non-finite radius-> NonFiniteRadius (radius == 0 is valid: self-hits only)
    //   * empty cloud      -> EmptyCloud
    //   * non-finite query -> NonFiniteQuery
    std::expected<std::vector<Neighbor>, IndexError> radius(float x, float y, float z,
                                                            float radius) const;

    std::expected<std::vector<Neighbor>, IndexError> radius(const Point3f& q, float r) const {
        return radius(q.x, q.y, q.z, r);
    }

private:
    // PIMPL: the nanoflann template machinery is an implementation detail kept out of the
    // public header (it would otherwise pull <nanoflann.hpp> — a third_party header not
    // held to the engine warning bar — into every consumer).
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool built_ = false;
};

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_POINT_INDEX_H
