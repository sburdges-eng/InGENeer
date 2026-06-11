#include "ingeneer/audit/sha256.hpp"

#include <cassert>
#include <cstring>

namespace ingeneer::audit {
namespace {

constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    // All SHA-256 schedule rotations use 0 < n < 32; n == 0 would make `x << 32` UB.
    assert(n > 0 && n < 32);
    return (x >> n) | (x << (32 - n));
}

}  // namespace

std::array<std::uint8_t, 32> sha256_raw(std::string_view data) {
    std::array<std::uint32_t, 8> h = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    const std::uint64_t bitlen = static_cast<std::uint64_t>(data.size()) * 8u;

    // Build the padded message: original || 0x80 || 0x00... || 64-bit big-endian length.
    std::string msg(data);
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0x00));
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bitlen >> (i * 8)) & 0xffu));
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(msg.data());
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t t = 0; t < 16; ++t) {
            w[t] = (static_cast<std::uint32_t>(bytes[off + t * 4 + 0]) << 24) |
                   (static_cast<std::uint32_t>(bytes[off + t * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(bytes[off + t * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(bytes[off + t * 4 + 3]));
        }
        for (std::size_t t = 16; t < 64; ++t) {
            const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (std::size_t t = 0; t < 64; ++t) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + kK[t] + w[t];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
}

std::string sha256_hex(std::string_view data) {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto raw = sha256_raw(data);
    std::string hex;
    hex.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        hex[i * 2 + 0] = kHex[(raw[i] >> 4) & 0xf];
        hex[i * 2 + 1] = kHex[raw[i] & 0xf];
    }
    return hex;
}

}  // namespace ingeneer::audit
