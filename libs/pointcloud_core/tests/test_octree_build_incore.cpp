// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_build_detail.h"
#include "ingeneer/pointcloud/octree_format.h"

#include <filesystem>

#include "check.hpp"
#include "octree_test_util.h"

using namespace ingeneer::pointcloud;
using namespace ingeneer::pointcloud::test;

static void run() {
    AabbD b = detail::compute_bounds(uniform_cloud(1000));
    CHECK(b.min[0] <= b.max[0] && b.min[2] <= b.max[2]);
    Quantizer q = detail::make_quantizer(b);
    CHECK(q.scale > 0.0);

    auto pts = uniform_cloud(50'000, 1000.0, 2'000'000.0, 500'000.0, 100.0, 7);
    BuildParams params;
    params.schema = AttributeSchema{};
    TempDir d1("incore_a"), d2("incore_b");
    auto r1 = build_octree_incore(pts, params, d1.path());
    auto r2 = build_octree_incore(pts, params, d2.path());
    CHECK(r1.has_value());
    CHECK(r2.has_value());
    CHECK_EQ(r1->point_count, static_cast<std::uint64_t>(50'000));
    CHECK(r1->node_count > 1);
    CHECK_EQ(r1->hierarchy_sha256, r2->hierarchy_sha256);
    CHECK_EQ(r1->points_sha256, r2->points_sha256);
    CHECK_EQ(r1->metadata_sha256, r2->metadata_sha256);

    CHECK(std::filesystem::exists(d1.path() / "metadata.json"));
    CHECK(std::filesystem::exists(d1.path() / "hierarchy.bin"));
    CHECK(std::filesystem::exists(d1.path() / "points.bin"));
    CHECK_EQ(r1->point_count, static_cast<std::uint64_t>(pts.size()));

    TempDir de("incore_empty");
    auto re = build_octree_incore(std::span<const RawPoint>(), params, de.path());
    CHECK(re.has_value());
    CHECK_EQ(re->point_count, static_cast<std::uint64_t>(0));
}

TEST_MAIN_RUN()
