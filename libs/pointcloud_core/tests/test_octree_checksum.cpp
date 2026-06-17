// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_checksum.h"

#include <array>
#include <cstddef>
#include <cstring>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static std::uint64_t hash_str(const char* s) {
    return xxh3_64(std::as_bytes(std::span<const char>(s, std::strlen(s))));
}

static void run() {
    CHECK_EQ(xxh3_64(std::span<const std::byte>()), static_cast<std::uint64_t>(0x2d06800538d394c2ull));
    CHECK_EQ(hash_str("INGOCT-node"), hash_str("INGOCT-node"));
    CHECK(hash_str("INGOCT-node") != hash_str("INGOCT-nodf"));
    CHECK(hash_str("abc") != hash_str("abcd"));
}

TEST_MAIN_RUN()
