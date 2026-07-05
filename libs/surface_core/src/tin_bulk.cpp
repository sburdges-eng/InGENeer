// SPDX-License-Identifier: Apache-2.0
//
// tin_bulk.cpp — spatially coherent bulk insertion (T11: BRIO rounds + Hilbert order).
//
// Tin::locate() is a remembering walk from the previous insert, which is O(√n) expected
// per RANDOM query but O(1) when consecutive queries are spatially adjacent (measured:
// see BENCHMARKS.md, "Random insertion is super-linear"). This TU therefore only REORDERS
// the batch before feeding the unchanged single-point insert() machinery:
//
//   * Hilbert order — xy quantized onto a 2^16 x 2^16 integer grid over the batch bbox
//     (purely a deterministic ordering heuristic; quantization NEVER feeds topology — all
//     topological decisions stay inside insert()'s exact predicates), then sorted by the
//     order-16 Hilbert index. Consecutive points are spatially adjacent, so each locate
//     walk starts at (or next to) the target.
//   * BRIO rounds (Amenta–Choi–Rote) — a pure Hilbert sweep inserts each point into a
//     triangulation of only its already-inserted Hilbert PREDECESSORS, whose union is a
//     crescent that can make individual cavity/flip work adversarially expensive. BRIO
//     restores the randomized-incremental cost bound by inserting a geometric cascade of
//     rounds (~n/2^R, ..., n/4, n/2 points), each round Hilbert-ordered, so every round
//     lands on a roughly uniform sample of the whole cloud.
//   * Round assignment is DETERMINISTIC (no RNG, C-4.6): point i draws a pseudo-random
//     64-bit value h = splitmix64(i) and is demoted countr_zero(h) rounds from the last
//     (a geometric(1/2) variable, exactly the BRIO coin-flip cascade). The assignment
//     depends only on the input INDEX through a full-avalanche mixer, so it is
//     uncorrelated with any coordinate pattern an adversary controls — adversarial-input
//     protection is preserved for every input not crafted against this one published
//     constant, which is the same determinism trade made by the repo's fixed-seed
//     benchmark/fuzz streams.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ingeneer/surface/tin.h"

namespace ingeneer::surface {
namespace {

// splitmix64 finalizer (public domain, Vigna). Full-avalanche: every input bit affects
// every output bit, decorrelating round assignment from input position.
std::uint64_t splitmix64(std::uint64_t z) noexcept {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Order-16 Hilbert index of a point on the 2^16 x 2^16 grid (classic iterative xy -> d
// transform; integer-only, deterministic).
std::uint32_t hilbert_d(std::uint32_t x, std::uint32_t y) noexcept {
    std::uint64_t d = 0;
    for (std::uint32_t s = 1u << 15; s > 0; s >>= 1) {
        const std::uint32_t rx = (x & s) ? 1u : 0u;
        const std::uint32_t ry = (y & s) ? 1u : 0u;
        d += static_cast<std::uint64_t>(s) * s * ((3u * rx) ^ ry);
        if (ry == 0) {  // rotate the quadrant
            if (rx == 1) {
                x = s - 1 - x;
                y = s - 1 - y;
            }
            const std::uint32_t t = x;
            x = y;
            y = t;
        }
    }
    return static_cast<std::uint32_t>(d);
}

// Quantize v in [lo, hi] to the 16-bit grid. Deterministic (pure double arithmetic on
// already-validated finite inputs); clamped so FP rounding can never escape the grid.
std::uint32_t quantize16(double v, double lo, double inv_extent) noexcept {
    const double t = (v - lo) * inv_extent;  // in [0, 65535] up to rounding
    if (t <= 0.0) return 0;
    if (t >= 65535.0) return 65535u;
    return static_cast<std::uint32_t>(t);
}

}  // namespace

std::expected<std::vector<VertexId>, TinError> Tin::insert_many(
    std::span<const TinVertex> pts) noexcept {
    // ---- up-front whole-batch validation (all-or-nothing; see tin.h) -------------------
    // These checks are exactly insert()'s input screens and depend on nothing but the
    // point itself, so rejecting here cannot diverge from per-point insertion. First
    // offending point in INPUT order wins (deterministic error identity).
    for (const TinVertex& p : pts) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            return std::unexpected(TinError{TinErrc::NonFiniteCoordinate});
        }
        if (!coordinate_in_domain(p.x) || !coordinate_in_domain(p.y) ||
            !coordinate_in_domain(p.z)) {
            return std::unexpected(TinError{TinErrc::CoordinateOutOfDomain});
        }
    }

    std::vector<VertexId> ids(pts.size(), kGhostVertex);
    if (pts.empty()) return ids;

    // ---- ordering keys ------------------------------------------------------------------
    // bbox of the batch (finite by validation above).
    double lox = pts[0].x, hix = pts[0].x, loy = pts[0].y, hiy = pts[0].y;
    for (const TinVertex& p : pts) {
        lox = p.x < lox ? p.x : lox;
        hix = p.x > hix ? p.x : hix;
        loy = p.y < loy ? p.y : loy;
        hiy = p.y > hiy ? p.y : hiy;
    }
    const double ex = hix - lox;
    const double ey = hiy - loy;
    const double inv_x = ex > 0.0 ? 65535.0 / ex : 0.0;  // degenerate extent -> cell 0
    const double inv_y = ey > 0.0 ? 65535.0 / ey : 0.0;

    // BRIO round count: last round holds ~n/2, halving downward until rounds would fall
    // below ~64 points; everything smaller pools into round 0.
    constexpr std::size_t kMinRound = 64;
    std::uint32_t rounds = 1;
    for (std::size_t m = pts.size(); m > 2 * kMinRound && rounds < 24; m /= 2) ++rounds;

    struct Key {
        std::uint32_t round;
        std::uint32_t hilbert;
        std::uint32_t index;  // input position; also the deterministic tie-break
    };
    std::vector<Key> order(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const std::uint32_t demote =
            static_cast<std::uint32_t>(std::countr_zero(splitmix64(static_cast<std::uint64_t>(i))));
        const std::uint32_t r = demote >= rounds - 1 ? 0u : rounds - 1 - demote;
        order[i] =
            Key{r, hilbert_d(quantize16(pts[i].x, lox, inv_x), quantize16(pts[i].y, loy, inv_y)),
                static_cast<std::uint32_t>(i)};
    }
    std::sort(order.begin(), order.end(), [](const Key& a, const Key& b) noexcept {
        if (a.round != b.round) return a.round < b.round;
        if (a.hilbert != b.hilbert) return a.hilbert < b.hilbert;
        return a.index < b.index;
    });

    // ---- driver: the unchanged single-point machinery, in BRIO/Hilbert order ----------
    // Snapshot for the only remaining failure path (insert()'s defensive WalkOverflow,
    // unreachable for in-domain input with sound predicates): restore and fail loudly
    // rather than expose a half-applied batch.
    const Tin snapshot(*this);
    for (const Key& k : order) {
        const TinVertex& p = pts[k.index];
        const auto id = insert(p.x, p.y, p.z);
        if (!id.has_value()) {
            *this = snapshot;
            return std::unexpected(id.error());
        }
        ids[k.index] = *id;
    }
    return ids;
}

}  // namespace ingeneer::surface
