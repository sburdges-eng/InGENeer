// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/morton.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ingeneer::pointcloud {

std::int32_t Quantizer::quantize(double v, int axis) const noexcept {
    const double q = std::floor((v - origin[axis]) / scale);
    if (q <= 0.0) return 0;
    constexpr double kMax = static_cast<double>((1u << kQuantBits) - 1u);
    if (q >= kMax) return static_cast<std::int32_t>((1u << kQuantBits) - 1u);
    return static_cast<std::int32_t>(q);
}

std::uint32_t top21(std::int32_t q) noexcept {
    return static_cast<std::uint32_t>(q) >> (kQuantBits - kMaxDepth);
}

namespace {

std::uint64_t split3(std::uint32_t v) noexcept {
    std::uint64_t x = v & 0x1FFFFFull;
    x = (x | (x << 32)) & 0x1F00000000FFFFull;
    x = (x | (x << 16)) & 0x1F0000FF0000FFull;
    x = (x | (x << 8)) & 0x100F00F00F00F00Full;
    x = (x | (x << 4)) & 0x10C30C30C30C30C3ull;
    x = (x | (x << 2)) & 0x1249249249249249ull;
    return x;
}

}  // namespace

std::uint64_t morton_encode_21(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (split3(x) << 2) | (split3(y) << 1) | split3(z);
}

std::uint8_t octant_at_level(std::uint64_t morton, std::uint8_t level) noexcept {
    if (morton == 0) return 0;
    const unsigned lz = static_cast<unsigned>(std::countl_zero(morton));
    const unsigned used_triples = (63u - lz + 2u) / 3u;
    if (level >= used_triples) return 0;
    const unsigned triple_index = (used_triples - 1u) - static_cast<unsigned>(level);
    return static_cast<std::uint8_t>((morton >> (triple_index * 3u)) & 0x7u);
}

}  // namespace ingeneer::pointcloud
