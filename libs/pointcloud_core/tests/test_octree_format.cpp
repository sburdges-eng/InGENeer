// SPDX-License-Identifier: Apache-2.0
// Format invariants: record size/layout, endianness guard, constant values.
#include "ingeneer/pointcloud/octree_format.h"

#include <bit>
#include <cstring>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    CHECK_EQ(sizeof(NodeRecordV1), static_cast<std::size_t>(32));
    CHECK_EQ(offsetof(NodeRecordV1, payload_offset), static_cast<std::size_t>(0));
    CHECK_EQ(offsetof(NodeRecordV1, payload_bytes), static_cast<std::size_t>(8));
    CHECK_EQ(offsetof(NodeRecordV1, point_count), static_cast<std::size_t>(12));
    CHECK_EQ(offsetof(NodeRecordV1, checksum_xxh3), static_cast<std::size_t>(16));
    CHECK_EQ(offsetof(NodeRecordV1, child_mask), static_cast<std::size_t>(24));
    CHECK_EQ(offsetof(NodeRecordV1, level), static_cast<std::size_t>(25));
    CHECK_EQ(offsetof(NodeRecordV1, flags), static_cast<std::size_t>(26));
    CHECK_EQ(offsetof(NodeRecordV1, first_child), static_cast<std::size_t>(28));

    CHECK(std::endian::native == std::endian::little);

    CHECK_EQ(kSamplingGrid, static_cast<std::uint32_t>(128));
    CHECK_EQ(kMaxNodePoints, static_cast<std::uint32_t>(20000));
    CHECK_EQ(kMaxDepth, static_cast<std::uint8_t>(21));
    CHECK_EQ(std::memcmp(kMagic, "INGOCT1", 7), 0);
    CHECK_EQ(kMagic[7], '\0');

    CHECK_EQ(kNoNode.v, static_cast<std::uint32_t>(0xFFFFFFFFu));
    CHECK(NodeId{5} == NodeId{5});
}

TEST_MAIN_RUN()
