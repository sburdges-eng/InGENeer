// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_checksum.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace ingeneer::pointcloud {

std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept {
    return XXH3_64bits(data.data(), data.size());
}

}  // namespace ingeneer::pointcloud
