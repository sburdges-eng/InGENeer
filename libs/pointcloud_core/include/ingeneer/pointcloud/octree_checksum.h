// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_checksum.h — XXH3-64 over a node payload (spec §3.4).
#ifndef INGENEER_POINTCLOUD_OCTREE_CHECKSUM_H
#define INGENEER_POINTCLOUD_OCTREE_CHECKSUM_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace ingeneer::pointcloud {

std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept;

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_CHECKSUM_H
