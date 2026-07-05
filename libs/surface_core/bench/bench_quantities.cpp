// Phase 6 exit — contour/volume perf baselines (see libs/surface_core/BENCHMARKS.md for
// recorded numbers and methodology). NOT a CTest; run manually from a hardened (optimized,
// sanitizer-free) build:
//
//   cmake --preset hardened && cmake --build --preset hardened --target bench_quantities
//   build/hardened/libs/surface_core/bench_quantities             # all workloads
//   build/hardened/libs/surface_core/bench_quantities contours    # one workload
//
// All surfaces are smooth synthetic height fields over fixed-seed LCG random xy
// (deterministic, C-4.6); TIN construction is NOT included in any measured time.
//
// Workloads:
//   contours    — 100k-point TIN, 20 evenly spaced levels: extract_contours total +
//                 per-level cost, then the chaikin_smooth(2) pass timed separately
//   volplane    — volume_to_plane over the 100k-point TIN at its mid elevation
//   volshared   — volume_between, shared-support fast path: two 100k TINs with identical
//                 xy (identical triangle sets) and different z fields
//   overlay10k  — volume_between, general overlay: two INDEPENDENT 10k triangulations
//                 (different xy seeds) over the same region
//   overlay50k  — same, 50k vs 50k (scaling probe for the bbox-prefilter pairing)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ingeneer/surface/contour.h"
#include "ingeneer/surface/tin.h"
#include "ingeneer/surface/volume.h"

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

// Smooth synthetic height field over [0, 10000]^2: a few long-wavelength undulations plus
// a gentle tilt, z in roughly [10, 95]. Smooth (not random per-point) so contour polylines
// are long and chaining/smoothing are exercised realistically.
double field_a(double x, double y) {
    return 50.0 + 30.0 * std::sin(x * 7e-4) * std::cos(y * 9e-4) + 1e-3 * x;
}

// Second field for volume_between: same character, different phase/amplitude, so the
// difference field d = a - b changes sign many times across the region (mixed-triangle
// splitting is exercised, not just one-sided prisms).
double field_b(double x, double y) {
    return 48.0 + 25.0 * std::sin(x * 9e-4 + 1.0) * std::cos(y * 7e-4 + 2.0) + 1.2e-3 * y;
}

// Build a TIN over `n` LCG-random xy points (seeded by `seed_salt`) with z = field(x, y).
Tin build_field_tin(std::size_t n, double (*field)(double, double), std::uint64_t seed_salt) {
    Lcg rng;
    rng.state += seed_salt;
    Tin tin;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = rng.coord(10000.0);
        const double y = rng.coord(10000.0);
        (void)tin.insert(x, y, field(x, y));
    }
    return tin;
}

void bench_contours() {
    constexpr std::size_t kPoints = 100000;
    constexpr int kLevels = 20;
    const Tin tin = build_field_tin(kPoints, field_a, 0);

    double zmin = tin.vertex(0).z, zmax = zmin;
    for (VertexId v = 1; v < tin.vertex_count(); ++v) {
        zmin = std::min(zmin, tin.vertex(v).z);
        zmax = std::max(zmax, tin.vertex(v).z);
    }
    // 20 levels strictly inside (zmin, zmax): i/21 of the range for i = 1..20.
    std::vector<double> levels(kLevels);
    for (std::size_t i = 0; i < levels.size(); ++i) {
        levels[i] = zmin + (zmax - zmin) * static_cast<double>(i + 1) / (kLevels + 1);
    }

    std::vector<ContourLevel> results;
    results.reserve(kLevels);
    const auto t0 = Clock::now();
    for (const double level : levels) {
        auto r = extract_contours(tin, level);
        results.push_back(std::move(r.value()));
    }
    const double secs = seconds_since(t0);

    std::size_t polylines = 0, segments = 0;
    for (const auto& lvl : results) {
        polylines += lvl.contours.size();
        for (const auto& c : lvl.contours) {
            segments += (c.points.size() - 1) + (c.closed ? 1u : 0u);
        }
    }
    std::printf(
        "%-12s  %9zu pts  %3d levels  %8.3f s  %8.2f ms/level  (polylines=%zu, "
        "segments=%zu)\n",
        "contours", kPoints, kLevels, secs, 1e3 * secs / kLevels, polylines, segments);

    // Chaikin smoothing pass (derived output), timed separately over the same contours.
    std::size_t smoothed_pts = 0;
    const auto t1 = Clock::now();
    for (const auto& lvl : results) {
        for (const auto& c : lvl.contours) {
            smoothed_pts += chaikin_smooth(c, 2).points.size();
        }
    }
    const double ssecs = seconds_since(t1);
    std::printf("%-12s  %9zu polylines  %8.3f ms  %8.3f ms/level  (smoothed pts=%zu)\n",
                "chaikin x2", polylines, 1e3 * ssecs, 1e3 * ssecs / kLevels, smoothed_pts);
}

void bench_volplane() {
    constexpr std::size_t kPoints = 100000;
    const Tin tin = build_field_tin(kPoints, field_a, 0);
    const auto t0 = Clock::now();
    const auto r = volume_to_plane(tin, 50.0);
    const double secs = seconds_since(t0);
    std::printf("%-12s  %9zu pts  %8.3f s  (cut=%.0f m3, fill=%.0f m3, area=%.0f m2)\n", "volplane",
                kPoints, secs, r->cut, r->fill, r->area);
}

void bench_volshared() {
    constexpr std::size_t kPoints = 100000;
    // Identical xy stream (same seed) => identical vertex ids and triangle sets, so
    // volume_between takes the documented O(n) shared-support fast path.
    const Tin existing = build_field_tin(kPoints, field_a, 0);
    const Tin design = build_field_tin(kPoints, field_b, 0);
    const auto t0 = Clock::now();
    const auto r = volume_between(design, existing);
    const double secs = seconds_since(t0);
    std::printf("%-12s  %9zu pts  %8.3f s  (cut=%.0f m3, fill=%.0f m3, area=%.0f m2)\n",
                "volshared", kPoints, secs, r->cut, r->fill, r->area);
}

void bench_overlay(const char* name, std::size_t n) {
    // Different seed salts => fully independent triangulations over the same region:
    // the general pairwise-overlay path (bbox-prefilter pairing + convex clipping).
    const Tin existing = build_field_tin(n, field_a, 0);
    const Tin design = build_field_tin(n, field_b, 0x5DEECE66Dull);
    const auto t0 = Clock::now();
    const auto r = volume_between(design, existing);
    const double secs = seconds_since(t0);
    std::printf("%-12s  %9zu x %zu pts  %8.3f s  (cut=%.0f m3, fill=%.0f m3, area=%.0f m2)\n", name,
                n, n, secs, r->cut, r->fill, r->area);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "all" || which == "contours") bench_contours();
    if (which == "all" || which == "volplane") bench_volplane();
    if (which == "all" || which == "volshared") bench_volshared();
    if (which == "all" || which == "overlay10k") bench_overlay("overlay10k", 10000);
    if (which == "all" || which == "overlay50k") bench_overlay("overlay50k", 50000);
    return 0;
}
