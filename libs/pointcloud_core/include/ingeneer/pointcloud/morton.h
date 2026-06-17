// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/morton.h — quantization + 63-bit Morton (21 levels x 3 bits).
#ifndef INGENEER_POINTCLOUD_MORTON_H
#define INGENEER_POINTCLOUD_MORTON_H

#include <cstdint>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

std::uint32_t top21(std::int32_t q) noexcept;
std::uint64_t morton_encode_21(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;
std::uint8_t octant_at_level(std::uint64_t morton, std::uint8_t level) noexcept;

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_MORTON_H
