// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/sha256.h — self-contained FIPS 180-4 SHA-256 for octree file hashes.
#ifndef INGENEER_POINTCLOUD_SHA256_H
#define INGENEER_POINTCLOUD_SHA256_H

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

std::array<std::uint8_t, 32> sha256_raw(std::string_view data);
std::string sha256_hex(std::string_view data);
std::expected<std::string, OctreeError> sha256_file_hex(const std::filesystem::path& path);

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_SHA256_H
