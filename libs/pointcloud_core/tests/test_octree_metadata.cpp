// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_metadata.h"

#include <string>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static OctreeMetadata sample() {
    OctreeMetadata m{};
    m.version_major = 1;
    m.version_minor = 0;
    m.origin[0] = 1000.5;
    m.origin[1] = -2000.25;
    m.origin[2] = 10.0;
    m.root_edge = 16384.0;
    m.scale = m.root_edge / 2147483648.0;
    m.root_cube_world = AabbD{{1000.5, -2000.25, 10.0}, {17384.5, 14383.75, 16394.0}};
    m.point_count = 100000;
    m.node_count = 37;
    m.max_level = 5;
    m.sampling_grid = 128;
    m.max_node_points = 20000;
    m.min_node_points = 1000;
    m.schema = AttributeSchema{.has_rgb = true, .has_gps_time = false};
    m.hierarchy_sha256 = std::string(64, 'a');
    m.points_sha256 = std::string(64, 'b');
    return m;
}

static void run() {
    const OctreeMetadata m = sample();
    const std::string js = write_metadata_json(m);

    CHECK_EQ(js, write_metadata_json(m));
    CHECK(js.find("\"format\": \"ingeneer-octree\"") != std::string::npos);

    auto rt = parse_metadata_json(js);
    CHECK(rt.has_value());
    CHECK_EQ(rt->point_count, m.point_count);
    CHECK_EQ(rt->node_count, m.node_count);
    CHECK_EQ(rt->max_node_points, m.max_node_points);
    CHECK_EQ(rt->origin[0], m.origin[0]);
    CHECK_EQ(rt->scale, m.scale);
    CHECK(rt->schema.has_rgb);
    CHECK(!rt->schema.has_gps_time);
    CHECK_EQ(rt->hierarchy_sha256, m.hierarchy_sha256);

    OctreeMetadata m2 = m;
    m2.version_major = 2;
    auto bad = parse_metadata_json(write_metadata_json(m2));
    CHECK(!bad.has_value());
    CHECK_EQ(bad.error().code, OctreeErrc::FormatVersion);

    CHECK(!parse_metadata_json("").has_value());
    CHECK(!parse_metadata_json("{").has_value());
    CHECK(!parse_metadata_json("{\"format\":\"ingeneer-octree\"}").has_value());
    CHECK_EQ(parse_metadata_json("not json at all").error().code, OctreeErrc::CorruptHierarchy);
}

TEST_MAIN_RUN()
