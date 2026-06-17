// SPDX-License-Identifier: Apache-2.0
// Quantization is the only FP step; Morton interleave + octant decode are pure integer.
#include "ingeneer/pointcloud/morton.h"

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    Quantizer q{.origin = {100.0, -50.0, 0.0}, .scale = 1.0};
    CHECK_EQ(q.quantize(100.0, 0), 0);
    CHECK_EQ(q.quantize(100.9, 0), 0);
    CHECK_EQ(q.quantize(105.0, 0), 5);
    CHECK_EQ(q.quantize(-50.0, 1), 0);
    CHECK_EQ(q.quantize(-49.0, 1), 1);
    CHECK_EQ(q.quantize(50.0, 2), 50);
    CHECK_EQ(q.quantize(99.0, 0), 0);
    Quantizer hi{.origin = {0, 0, 0}, .scale = 1.0};
    CHECK_EQ(hi.quantize(1e30, 0), static_cast<std::int32_t>((1u << 31) - 1));

    CHECK_EQ(morton_encode_21(0, 0, 0), static_cast<std::uint64_t>(0));
    CHECK_EQ(morton_encode_21(1, 1, 1), static_cast<std::uint64_t>(0b111));
    CHECK_EQ(morton_encode_21(1, 0, 0), static_cast<std::uint64_t>(0b100));
    CHECK_EQ(morton_encode_21(0, 0, 1), static_cast<std::uint64_t>(0b001));
    CHECK_EQ(morton_encode_21(2, 0, 0), static_cast<std::uint64_t>(0b100'000));

    const std::uint64_t code = morton_encode_21(0b101, 0b010, 0b001);
    CHECK_EQ(octant_at_level(code, 0), static_cast<std::uint8_t>(0b100));
    CHECK_EQ(octant_at_level(code, 1), static_cast<std::uint8_t>(0b010));
    CHECK_EQ(octant_at_level(code, 2), static_cast<std::uint8_t>(0b101));

    CHECK_EQ(top21(static_cast<std::int32_t>(1u << 30)), static_cast<std::uint32_t>(1u << 20));
}

TEST_MAIN_RUN()
