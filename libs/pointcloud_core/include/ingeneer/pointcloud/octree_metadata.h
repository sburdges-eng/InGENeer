// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_metadata.h — metadata.json model + (de)serialization (spec §3.1).
#ifndef INGENEER_POINTCLOUD_OCTREE_METADATA_H
#define INGENEER_POINTCLOUD_OCTREE_METADATA_H

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

struct OctreeMetadata {
    std::uint32_t version_major = 1;
    std::uint32_t version_minor = 0;
    double origin[3] = {0, 0, 0};
    double scale = 0.0;
    double root_edge = 0.0;
    AabbD root_cube_world{};
    std::uint64_t point_count = 0;
    std::uint32_t node_count = 0;
    std::uint8_t max_level = 0;
    std::uint32_t sampling_grid = kSamplingGrid;
    std::uint32_t max_node_points = kMaxNodePoints;
    std::uint32_t min_node_points = kMinNodePoints;
    AttributeSchema schema{};
    std::string hierarchy_sha256;
    std::string points_sha256;
};

std::string write_metadata_json(const OctreeMetadata& m);
std::expected<OctreeMetadata, OctreeError> parse_metadata_json(std::string_view text);

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_METADATA_H
