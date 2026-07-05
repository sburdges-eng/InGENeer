// SPDX-License-Identifier: Apache-2.0
//
// In-core nanoflann spatial index (Phase 7). See ingeneer/pointcloud/point_index.h.
//
// nanoflann (BSD) is consumed here as the third_party_nanoflann INTERFACE target. Per the
// vendoring discipline (see libs/geometry_core/CMakeLists.txt) the nanoflann header is not
// held to the engine's -Wconversion/-Wshadow bar; we therefore confine its inclusion to
// this single TU behind a PIMPL so no consumer drags the header in.

#include "ingeneer/pointcloud/point_index.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include <nanoflann.hpp>

namespace ingeneer::pointcloud {

namespace {

// nanoflann dataset adaptor over a PointCloud's SoA-of-points position buffer. Implements
// the standard adaptor surface: point count, per-component access, and an opt-out bbox
// hint. References the cloud's positions span directly — no copy (bulk-rebuild contract:
// the cloud outlives the index and is not mutated while queried).
struct CloudAdaptor {
    std::span<const Point3f> pts;

    std::size_t kdtree_get_point_count() const { return pts.size(); }

    float kdtree_get_pt(std::size_t i, std::size_t dim) const {
        const Point3f& p = pts[i];
        switch (dim) {
            case 0:
                return p.x;
            case 1:
                return p.y;
            default:
                return p.z;
        }
    }

    // Let nanoflann compute the bounding box itself.
    template <class BBox>
    bool kdtree_get_bbox(BBox&) const {
        return false;
    }
};

using KdTree =
    nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, CloudAdaptor>,
                                        CloudAdaptor, 3, std::uint32_t>;

// We want the conventional INCLUSIVE radius contract (distance <= radius), but nanoflann's
// machinery is exclusive on the boundary at two levels:
//   1. the stock RadiusResultSet ACCEPTS with a strict `dist < radius`, and
//   2. the kd-tree TRAVERSAL prunes a branch whose splitting-plane distance reaches
//      worstDist(), so a point sitting exactly on the radius is never even visited.
// (1) alone is not enough — observed: radius 1.0 around the origin returned only the
// coincident point, the d^2 == 1 points were pruned in traversal. So we widen the
// traversal bound by one ULP via worstDist() (nextafter), guaranteeing boundary candidates
// are visited, while keeping an EXACT `<=` accept test against the true radius. The widened
// bound only affects which candidates are EXAMINED; the accept test alone decides the
// result, so correctness and determinism (pure function of point order + tree) hold.
struct InclusiveRadiusResultSet {
    using DistanceType = float;
    using IndexType = std::uint32_t;

    const float radius_sq;      // exact accept threshold
    const float worst_dist_sq;  // traversal bound, one ULP above radius_sq
    std::vector<nanoflann::ResultItem<std::uint32_t, float>>& items;

    InclusiveRadiusResultSet(float r_sq,
                             std::vector<nanoflann::ResultItem<std::uint32_t, float>>& out)
        : radius_sq(r_sq),
          worst_dist_sq(std::nextafter(r_sq, std::numeric_limits<float>::infinity())),
          items(out) {
        items.clear();
    }

    std::size_t size() const { return items.size(); }
    bool empty() const { return items.empty(); }
    bool full() const { return true; }

    bool addPoint(float dist, std::uint32_t index) {
        if (dist <= radius_sq) {
            items.emplace_back(index, dist);
        }
        return true;
    }

    float worstDist() const { return worst_dist_sq; }

    void sort() { std::sort(items.begin(), items.end(), nanoflann::IndexDist_Sorter()); }
};

inline bool finite3(float x, float y, float z) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

}  // namespace

struct SpatialIndex::Impl {
    CloudAdaptor adaptor;
    std::unique_ptr<KdTree> tree;
};

SpatialIndex::SpatialIndex() noexcept = default;
SpatialIndex::~SpatialIndex() = default;
SpatialIndex::SpatialIndex(SpatialIndex&&) noexcept = default;
SpatialIndex& SpatialIndex::operator=(SpatialIndex&&) noexcept = default;

SpatialIndex::SpatialIndex(const PointCloud& cloud) : SpatialIndex() { rebuild(cloud); }

void SpatialIndex::rebuild(const PointCloud& cloud) {
    // Discard any previous tree and build afresh over the cloud's current contents
    // (bulk rebuild, not incremental — the plan's contract).
    impl_ = std::make_unique<Impl>();
    impl_->adaptor.pts = cloud.positions();
    // A leaf size of 10 matches the geometry_core smoke-test parameterisation and is
    // nanoflann's common default for point-cloud workloads.
    impl_->tree =
        std::make_unique<KdTree>(3, impl_->adaptor, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    built_ = true;
}

std::size_t SpatialIndex::point_count() const noexcept {
    return impl_ ? impl_->adaptor.pts.size() : 0;
}

std::expected<std::vector<Neighbor>, IndexError> SpatialIndex::knn(float x, float y, float z,
                                                                   std::size_t k) const {
    if (!built_ || !impl_ || !impl_->tree) {
        return std::unexpected(IndexError{IndexErrc::IndexNotBuilt});
    }
    if (k == 0) {
        return std::unexpected(IndexError{IndexErrc::InvalidK});
    }
    if (impl_->adaptor.pts.empty()) {
        return std::unexpected(IndexError{IndexErrc::EmptyCloud});
    }
    if (!finite3(x, y, z)) {
        return std::unexpected(IndexError{IndexErrc::NonFiniteQuery});
    }

    const std::size_t want = std::min(k, impl_->adaptor.pts.size());
    std::vector<std::uint32_t> indices(want);
    std::vector<float> dists(want);
    const float query[3] = {x, y, z};

    // knnSearch returns distance-sorted results; N <= want (== want here since the cloud
    // has >= want points).
    const std::size_t found = impl_->tree->knnSearch(query, want, indices.data(), dists.data());

    std::vector<Neighbor> out;
    out.reserve(found);
    for (std::size_t i = 0; i < found; ++i) {
        out.push_back(Neighbor{indices[i], dists[i]});
    }
    return out;
}

std::expected<std::vector<Neighbor>, IndexError> SpatialIndex::radius(float x, float y, float z,
                                                                      float r) const {
    if (!built_ || !impl_ || !impl_->tree) {
        return std::unexpected(IndexError{IndexErrc::IndexNotBuilt});
    }
    if (std::isnan(r) || std::isinf(r)) {
        return std::unexpected(IndexError{IndexErrc::NonFiniteRadius});
    }
    if (r < 0.0F) {
        return std::unexpected(IndexError{IndexErrc::NegativeRadius});
    }
    if (impl_->adaptor.pts.empty()) {
        return std::unexpected(IndexError{IndexErrc::EmptyCloud});
    }
    if (!finite3(x, y, z)) {
        return std::unexpected(IndexError{IndexErrc::NonFiniteQuery});
    }

    // L2_Simple_Adaptor works in SQUARED distances: the radius threshold and every returned
    // distance are squared. We use our InclusiveRadiusResultSet so the boundary is INCLUSIVE
    // (dist_sq <= r^2) — a point at exactly `r` is returned, and radius == 0 returns a
    // coincident point (the stock nanoflann set's strict `<` would drop both).
    const float r_sq = r * r;
    const float query[3] = {x, y, z};

    std::vector<nanoflann::ResultItem<std::uint32_t, float>> matches;
    InclusiveRadiusResultSet result(r_sq, matches);
    nanoflann::SearchParameters params;  // sorted flag unused by the custom callback path
    const std::size_t found =
        impl_->tree->template radiusSearchCustomCallback<InclusiveRadiusResultSet>(query, result,
                                                                                   params);
    (void)found;  // == matches.size(); we read the vector directly
    // Sort ascending by squared distance (deterministic IndexDist tie order) — the documented
    // ordering guarantee for radius results.
    result.sort();

    std::vector<Neighbor> out;
    out.reserve(matches.size());
    for (const auto& m : matches) {
        out.push_back(Neighbor{m.first, m.second});
    }
    return out;
}

}  // namespace ingeneer::pointcloud
