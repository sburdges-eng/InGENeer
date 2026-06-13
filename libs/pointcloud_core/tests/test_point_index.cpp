// SPDX-License-Identifier: Apache-2.0
//
// SpatialIndex tests (Phase 7): known-answer k-NN (k=1, k=3) and radius on a small fixed
// set; coincident-query (self at distance 0); empty + single-point edge cases;
// rebuild-after-mutation (bulk-rebuild semantics); and a 100k deterministic synthetic
// cloud validated against an O(n) brute-force reference on sampled queries.

#include "ingeneer/pointcloud/point_index.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "check.hpp"
#include "ingeneer/pointcloud/point_cloud.h"

using namespace ingeneer::pointcloud;

namespace {

float dist_sq(const Point3f& a, float x, float y, float z) {
    const float dx = a.x - x;
    const float dy = a.y - y;
    const float dz = a.z - z;
    return dx * dx + dy * dy + dz * dz;
}

// Brute-force k-NN reference: returns point ids sorted by (dist_sq, id). The (dist_sq, id)
// tie-break is the reference's OWN deterministic order; we compare the SET of ids at each
// distance level against the index (which has its own deterministic tie order), never the
// raw id order, since nanoflann does not promise id-order among equidistant points.
std::vector<std::pair<float, PointId>> brute_knn(const PointCloud& pc, float x, float y, float z,
                                                 std::size_t k) {
    std::vector<std::pair<float, PointId>> all;
    all.reserve(pc.point_count());
    for (PointId i = 0; i < pc.point_count(); ++i) {
        all.emplace_back(dist_sq(pc.position(i), x, y, z), i);
    }
    std::sort(all.begin(), all.end());
    if (all.size() > k) {
        all.resize(k);
    }
    return all;
}

}  // namespace

static void run() {
    // Fixed unit-grid-ish set with known geometry.
    //   id: 0       1       2       3       4
    //   xy: (0,0)  (1,0)   (0,1)   (1,1)   (2,2)
    PointCloud pc;
    (void)pc.add_point(0.0F, 0.0F, 0.0F);  // 0
    (void)pc.add_point(1.0F, 0.0F, 0.0F);  // 1
    (void)pc.add_point(0.0F, 1.0F, 0.0F);  // 2
    (void)pc.add_point(1.0F, 1.0F, 0.0F);  // 3
    (void)pc.add_point(2.0F, 2.0F, 0.0F);  // 4

    SpatialIndex idx(pc);
    CHECK(idx.is_built());
    CHECK_EQ(idx.point_count(), static_cast<std::size_t>(5));

    // --- k=1 known answer: nearest to (0.1, 0.1, 0) is point 0 -------------------------
    {
        auto r = idx.knn(0.1F, 0.1F, 0.0F, 1);
        CHECK(r.has_value());
        CHECK_EQ(r->size(), static_cast<std::size_t>(1));
        CHECK_EQ((*r)[0].id, static_cast<PointId>(0));
    }

    // --- k=3 known answer: nearest 3 to (0,0,0) are {0,1,2} (1 and 2 tie at d^2=1) -----
    {
        auto r = idx.knn(0.0F, 0.0F, 0.0F, 3);
        CHECK(r.has_value());
        CHECK_EQ(r->size(), static_cast<std::size_t>(3));
        // Distance-sorted: first is the self-hit at 0.
        CHECK_EQ((*r)[0].id, static_cast<PointId>(0));
        CHECK_EQ((*r)[0].dist_sq, 0.0F);
        // The other two are points 1 and 2 (order among the tie is nanoflann's choice).
        std::vector<PointId> got{(*r)[1].id, (*r)[2].id};
        std::sort(got.begin(), got.end());
        CHECK_EQ(got[0], static_cast<PointId>(1));
        CHECK_EQ(got[1], static_cast<PointId>(2));
        CHECK_EQ((*r)[1].dist_sq, 1.0F);
        CHECK_EQ((*r)[2].dist_sq, 1.0F);
    }

    // --- coincident query: self at distance 0 ------------------------------------------
    {
        auto r = idx.knn(1.0F, 1.0F, 0.0F, 1);  // exactly point 3
        CHECK(r.has_value());
        CHECK_EQ((*r)[0].id, static_cast<PointId>(3));
        CHECK_EQ((*r)[0].dist_sq, 0.0F);
    }

    // --- radius query (inclusive boundary, squared distances reported) -----------------
    {
        // radius 1.0 around (0,0,0): points 0 (d=0) and 1,2 (d=1, on the boundary).
        auto r = idx.radius(0.0F, 0.0F, 0.0F, 1.0F);
        CHECK(r.has_value());
        CHECK_EQ(r->size(), static_cast<std::size_t>(3));
        // Sorted ascending by dist_sq: first is the self-hit.
        CHECK_EQ((*r)[0].dist_sq, 0.0F);
        CHECK_EQ((*r)[0].id, static_cast<PointId>(0));
        std::vector<PointId> got{(*r)[1].id, (*r)[2].id};
        std::sort(got.begin(), got.end());
        CHECK_EQ(got[0], static_cast<PointId>(1));
        CHECK_EQ(got[1], static_cast<PointId>(2));

        // radius 0.5 around (0,0,0): only the self-hit (others at d=1 > 0.25 squared).
        auto r0 = idx.radius(0.0F, 0.0F, 0.0F, 0.5F);
        CHECK(r0.has_value());
        CHECK_EQ(r0->size(), static_cast<std::size_t>(1));
        CHECK_EQ((*r0)[0].id, static_cast<PointId>(0));

        // radius 0 around point 4: just point 4 itself.
        auto rz = idx.radius(2.0F, 2.0F, 0.0F, 0.0F);
        CHECK(rz.has_value());
        CHECK_EQ(rz->size(), static_cast<std::size_t>(1));
        CHECK_EQ((*rz)[0].id, static_cast<PointId>(4));
    }

    // --- error paths -------------------------------------------------------------------
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        CHECK_EQ(idx.knn(0, 0, 0, 0).error().code, IndexErrc::InvalidK);
        CHECK_EQ(idx.knn(nan, 0, 0, 1).error().code, IndexErrc::NonFiniteQuery);
        CHECK_EQ(idx.radius(0, 0, 0, -1.0F).error().code, IndexErrc::NegativeRadius);
        CHECK_EQ(idx.radius(0, 0, 0, inf).error().code, IndexErrc::NonFiniteRadius);
        CHECK_EQ(idx.radius(nan, 0, 0, 1.0F).error().code, IndexErrc::NonFiniteQuery);
    }

    // --- empty cloud: no crash, sensible error -----------------------------------------
    {
        PointCloud empty;
        SpatialIndex eidx(empty);
        CHECK(eidx.is_built());
        CHECK_EQ(eidx.point_count(), static_cast<std::size_t>(0));
        CHECK_EQ(eidx.knn(0, 0, 0, 1).error().code, IndexErrc::EmptyCloud);
        CHECK_EQ(eidx.radius(0, 0, 0, 1.0F).error().code, IndexErrc::EmptyCloud);
    }

    // --- default-constructed (not built) index: error, no crash ------------------------
    {
        SpatialIndex unbuilt;
        CHECK(!unbuilt.is_built());
        CHECK_EQ(unbuilt.knn(0, 0, 0, 1).error().code, IndexErrc::IndexNotBuilt);
        CHECK_EQ(unbuilt.radius(0, 0, 0, 1.0F).error().code, IndexErrc::IndexNotBuilt);
    }

    // --- single-point cloud ------------------------------------------------------------
    {
        PointCloud one;
        (void)one.add_point(5.0F, 5.0F, 5.0F);
        SpatialIndex oidx(one);
        // k larger than the cloud returns only what exists (1 point).
        auto r = oidx.knn(0, 0, 0, 4);
        CHECK(r.has_value());
        CHECK_EQ(r->size(), static_cast<std::size_t>(1));
        CHECK_EQ((*r)[0].id, static_cast<PointId>(0));
    }

    // --- rebuild after mutation: index reflects the NEW contents -----------------------
    {
        PointCloud m;
        (void)m.add_point(0, 0, 0);  // 0
        SpatialIndex midx(m);
        // Before mutation: nearest to (10,10,10) is the only point, id 0.
        CHECK_EQ((*midx.knn(10, 10, 10, 1))[0].id, static_cast<PointId>(0));

        (void)m.add_point(10, 10, 10);  // 1 (the cloud buffer may have reallocated)
        midx.rebuild(m);                // bulk rebuild — the only way to see the new point
        CHECK_EQ(midx.point_count(), static_cast<std::size_t>(2));
        auto r = midx.knn(10, 10, 10, 1);
        CHECK(r.has_value());
        CHECK_EQ((*r)[0].id, static_cast<PointId>(1));
        CHECK_EQ((*r)[0].dist_sq, 0.0F);
    }

    // --- 100k deterministic synthetic cloud vs brute-force reference --------------------
    // A reproducible LCG (NOT std::random — determinism, no library RNG variance) fills a
    // 100k-point cloud; we validate k-NN and radius on a sample of queries against the
    // O(n) brute force. This both checks correctness and documents the index contract.
    {
        PointCloud big;
        big.reserve(100000);
        std::uint64_t s = 0x9E3779B97F4A7C15ULL;  // fixed seed
        auto next = [&s]() -> float {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            // top 24 bits -> [0,1); scale to a 1000-unit cube.
            const std::uint32_t bits = static_cast<std::uint32_t>(s >> 40);
            return (static_cast<float>(bits) / 16777216.0F) * 1000.0F;
        };
        for (int i = 0; i < 100000; ++i) {
            (void)big.add_point(next(), next(), next());
        }
        SpatialIndex bidx(big);
        CHECK_EQ(bidx.point_count(), static_cast<std::size_t>(100000));

        // Sample 32 deterministic query points (reuse the same LCG stream).
        bool knn_ok = true;
        bool radius_ok = true;
        for (int q = 0; q < 32; ++q) {
            const float qx = next();
            const float qy = next();
            const float qz = next();

            // k-NN: the SET of returned ids must equal the brute-force k-nearest set, and
            // distances must be non-decreasing. We compare as sets to stay tie-agnostic;
            // we additionally require the k-th index distance to equal the k-th brute
            // distance (the boundary is what tie-order could disagree on).
            const std::size_t k = 8;
            auto got = bidx.knn(qx, qy, qz, k);
            if (!got.has_value() || got->size() != k) {
                knn_ok = false;
            } else {
                auto ref = brute_knn(big, qx, qy, qz, k);
                // Distances must match elementwise (distance is order-invariant under ties).
                for (std::size_t i = 0; i < k; ++i) {
                    if (std::abs((*got)[i].dist_sq - ref[i].first) > 1e-3F) {
                        knn_ok = false;
                    }
                    if (i > 0 && (*got)[i].dist_sq + 1e-4F < (*got)[i - 1].dist_sq) {
                        knn_ok = false;  // not non-decreasing
                    }
                }
            }

            // Radius: returned id set must equal the brute-force set within the radius.
            const float r = 25.0F;
            const float r_sq = r * r;
            auto gr = bidx.radius(qx, qy, qz, r);
            if (!gr.has_value()) {
                radius_ok = false;
            } else {
                std::vector<PointId> got_ids;
                got_ids.reserve(gr->size());
                for (const auto& n : *gr) {
                    got_ids.push_back(n.id);
                    if (n.dist_sq > r_sq + 1e-2F) {
                        radius_ok = false;  // outside the radius
                    }
                }
                std::sort(got_ids.begin(), got_ids.end());

                std::vector<PointId> ref_ids;
                for (PointId i = 0; i < big.point_count(); ++i) {
                    if (dist_sq(big.position(i), qx, qy, qz) <= r_sq) {
                        ref_ids.push_back(i);
                    }
                }
                std::sort(ref_ids.begin(), ref_ids.end());
                if (got_ids != ref_ids) {
                    radius_ok = false;
                }
            }
        }
        CHECK(knn_ok);
        CHECK(radius_ok);
    }
}

TEST_MAIN_RUN()
