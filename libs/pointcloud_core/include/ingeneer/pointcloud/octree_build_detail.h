// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_build_detail.h — in-core build helpers + Pass-2 entry point.
#ifndef INGENEER_POINTCLOUD_OCTREE_BUILD_DETAIL_H
#define INGENEER_POINTCLOUD_OCTREE_BUILD_DETAIL_H

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

std::expected<BuildReport, OctreeError> build_octree_incore(std::span<const RawPoint> points,
                                                            const BuildParams& params,
                                                            const std::filesystem::path& out_dir);

namespace detail {

AabbD compute_bounds(std::span<const RawPoint> points) noexcept;
Quantizer make_quantizer(const AabbD& world) noexcept;

std::uint8_t grid_cell_axis(std::int32_t q, std::uint8_t level) noexcept;

std::uint64_t cell_dist_sq(std::int32_t qx, std::int32_t qy, std::int32_t qz, std::uint8_t level,
                           std::int32_t lox, std::int32_t loy, std::int32_t loz, std::uint8_t cx,
                           std::uint8_t cy, std::uint8_t cz) noexcept;

}  // namespace detail
}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_BUILD_DETAIL_H
