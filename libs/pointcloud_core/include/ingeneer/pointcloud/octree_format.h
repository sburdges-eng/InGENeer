// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_format.h — INGOCT v1 on-disk wire contract + shared types.
// Spec: docs/superpowers/specs/2026-06-11-out-of-core-octree-design.md §3, §6.1.
#ifndef INGENEER_POINTCLOUD_OCTREE_FORMAT_H
#define INGENEER_POINTCLOUD_OCTREE_FORMAT_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

static_assert(std::endian::native == std::endian::little,
              "INGOCT v1 is little-endian only (spec §3.4); big-endian builds are rejected.");

namespace ingeneer::pointcloud {

struct NodeId {
    std::uint32_t v;
};
inline constexpr NodeId kNoNode{0xFFFFFFFFu};
inline constexpr bool operator==(NodeId a, NodeId b) noexcept { return a.v == b.v; }

struct NodeBufferHandle {
    std::uint32_t slot;
    std::uint32_t generation;
};

enum class OctreeErrc : std::uint8_t {
    Io,
    FormatVersion,
    ChecksumMismatch,
    CorruptHierarchy,
    BudgetExhausted,
    StaleHandle,
    InvalidNode,
    BuildFailed,
};
struct OctreeError {
    OctreeErrc code;
    std::uint32_t node = kNoNode.v;
};

inline constexpr std::uint32_t kSamplingGrid = 128;
inline constexpr std::uint32_t kMaxNodePoints = 20'000;
inline constexpr std::uint32_t kMinNodePoints = 1'000;
inline constexpr std::uint8_t kMaxDepth = 21;
inline constexpr std::size_t kPayloadAlign = 16'384;
inline constexpr std::uint32_t kQuantBits = 31;
inline constexpr char kMagic[8] = {'I', 'N', 'G', 'O', 'C', 'T', '1', '\0'};

#pragma pack(push, 1)
struct NodeRecordV1 {
    std::uint64_t payload_offset;
    std::uint32_t payload_bytes;
    std::uint32_t point_count;
    std::uint64_t checksum_xxh3;
    std::uint8_t child_mask;
    std::uint8_t level;
    std::uint16_t flags;
    std::uint32_t first_child;
};
#pragma pack(pop)
static_assert(sizeof(NodeRecordV1) == 32);

struct RawPoint {
    double x, y, z;
    std::uint16_t intensity = 0;
    std::uint8_t classification = 0;
    std::uint8_t return_flags = 0;
    std::uint16_t rgb[3] = {0, 0, 0};
    double gps_time = 0.0;
};

struct AttributeSchema {
    bool has_rgb = false;
    bool has_gps_time = false;
};

struct AabbD {
    double min[3];
    double max[3];
};

struct Quantizer {
    double origin[3];
    double scale;
    std::int32_t quantize(double v, int axis) const noexcept;
};

struct BuildParams {
    AttributeSchema schema;
    std::uint32_t sampling_grid = kSamplingGrid;
    std::uint32_t max_node_points = kMaxNodePoints;
    std::uint32_t min_node_points = kMinNodePoints;
    std::uint64_t chunk_cap_bytes = 256ull * 1024 * 1024;
    std::uint32_t worker_count = 1;
};

struct BuildReport {
    std::uint64_t point_count = 0;
    std::uint32_t node_count = 0;
    std::uint8_t max_level = 0;
    std::string hierarchy_sha256;
    std::string points_sha256;
    std::string metadata_sha256;
};

struct NodeMeta {
    std::uint32_t point_count;
    std::uint8_t level;
    std::uint8_t child_mask;
    AabbD aabb;
};

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_FORMAT_H
