// Phase 6.5 — TIN perf baselines (see libs/surface_core/BENCHMARKS.md for recorded
// numbers and methodology). NOT a CTest; run manually from a hardened (optimized,
// sanitizer-free) build:
//
//   cmake --preset hardened && cmake --build --preset hardened --target bench_tin
//   build/hardened/libs/surface_core/bench_tin            # all workloads
//   build/hardened/libs/surface_core/bench_tin random1m   # one workload
//
// Workloads (fixed-seed LCG; deterministic, C-4.6):
//   random10k / random100k / random1m — uniform random insertion (points/sec)
//   lattice100k                       — shuffled 317x317 integer lattice (cocircular
//                                       tie stress: every unit square is a tie)
//   roadway100k                       — collinear-heavy survey pattern: 13 lane offsets
//                                       x 7693 stations (collinear rows AND columns)
//   breaklines                        — 1000 two-point Split-policy breaklines into a
//                                       100k random TIN (per-breakline cost)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;
using Clock = std::chrono::steady_clock;

namespace {

struct Lcg {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 33);
    }
    double coord(double scale) { return (static_cast<double>(next()) / 4294967296.0) * scale; }
};

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

void report(const char* name, std::size_t n, double secs, const Tin& tin) {
    std::printf("%-12s  %9zu pts  %8.3f s  %12.0f pts/s  (tris=%zu, hull=%zu)\n", name, n, secs,
                static_cast<double>(n) / secs, tin.triangle_count(), tin.hull_size());
}

void bench_random(const char* name, std::size_t n) {
    Lcg rng;
    std::vector<TinVertex> pts(n);
    for (auto& p : pts) p = {rng.coord(10000.0), rng.coord(10000.0), rng.coord(100.0)};
    Tin tin;
    const auto t0 = Clock::now();
    for (const auto& p : pts) (void)tin.insert(p.x, p.y, p.z);
    report(name, n, seconds_since(t0), tin);
}

void bench_lattice() {
    constexpr int kSide = 317;  // 317^2 = 100489 points
    Lcg rng;
    std::vector<std::pair<int, int>> pts;
    pts.reserve(static_cast<std::size_t>(kSide) * kSide);
    for (int x = 0; x < kSide; ++x) {
        for (int y = 0; y < kSide; ++y) pts.emplace_back(x, y);
    }
    for (std::size_t i = pts.size(); i > 1; --i) {  // deterministic Fisher-Yates
        std::swap(pts[i - 1], pts[rng.next() % i]);
    }
    Tin tin;
    const auto t0 = Clock::now();
    for (const auto& [x, y] : pts) {
        (void)tin.insert(static_cast<double>(x), static_cast<double>(y),
                         static_cast<double>(x + y));
    }
    report("lattice100k", pts.size(), seconds_since(t0), tin);
}

void bench_roadway() {
    // Survey-style collinear-heavy pattern: stations every 1.3 m along a straight
    // alignment, 13 lane offsets per station -> collinear rows and columns everywhere.
    constexpr int kStations = 7693;
    constexpr int kLanes = 13;  // offsets -15..+15 step 2.5
    std::vector<TinVertex> pts;
    pts.reserve(static_cast<std::size_t>(kStations) * kLanes);
    for (int s = 0; s < kStations; ++s) {
        for (int l = 0; l < kLanes; ++l) {
            const double x = 1.3 * static_cast<double>(s);
            const double y = -15.0 + 2.5 * static_cast<double>(l);
            pts.push_back({x, y, 0.01 * x + 0.1 * y});
        }
    }
    Tin tin;
    const auto t0 = Clock::now();
    for (const auto& p : pts) (void)tin.insert(p.x, p.y, p.z);
    report("roadway100k", pts.size(), seconds_since(t0), tin);
}

void bench_breaklines() {
    constexpr std::size_t kBase = 100000;
    constexpr std::size_t kLines = 1000;
    Lcg rng;
    Tin tin;
    for (std::size_t i = 0; i < kBase; ++i) {
        (void)tin.insert(rng.coord(10000.0), rng.coord(10000.0), rng.coord(100.0));
    }
    std::vector<std::array<TinVertex, 2>> lines(kLines);
    for (auto& ln : lines) {
        // Short segments (~100 m) well inside the domain.
        const double x0 = 100.0 + rng.coord(9700.0);
        const double y0 = 100.0 + rng.coord(9700.0);
        ln[0] = {x0, y0, rng.coord(100.0)};
        ln[1] = {x0 + rng.coord(100.0) - 50.0, y0 + rng.coord(100.0) - 50.0, rng.coord(100.0)};
    }
    std::size_t ok = 0;
    const auto t0 = Clock::now();
    for (const auto& ln : lines) {
        if (tin.insert_breakline(ln, CrossingPolicy::Split)) ++ok;
    }
    const double secs = seconds_since(t0);
    std::printf(
        "%-12s  %9zu into 100k TIN  %8.3f s  %10.1f us/breakline  (ok=%zu, "
        "constrained=%zu)\n",
        "breaklines", kLines, secs, 1e6 * secs / static_cast<double>(kLines), ok,
        tin.constrained_edge_count());
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "all" || which == "random10k") bench_random("random10k", 10000);
    if (which == "all" || which == "random100k") bench_random("random100k", 100000);
    if (which == "all" || which == "random1m") bench_random("random1m", 1000000);
    if (which == "all" || which == "lattice100k") bench_lattice();
    if (which == "all" || which == "roadway100k") bench_roadway();
    if (which == "all" || which == "breaklines") bench_breaklines();
    return 0;
}
