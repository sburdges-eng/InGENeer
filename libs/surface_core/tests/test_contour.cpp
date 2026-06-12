// surface_core contours — analytic known answers + TOTaLi oracle parity (Phase 6.3).
//
// Analytic cases: planar ramp (contours are exact straight lines, chained into a single
// hull-terminated polyline), closed contour around a peak, vertex-exactly-on-level
// degeneracy (symbolic perturbation z+eps), levels outside the z range (empty result, not
// an error), Chaikin smoothing tagged derived.
//
// Oracle parity: fixture extracted by tools/oracle/extract_from_totali.py replicating
// TOTaLi's _contour_at_elevation verbatim over the pinned 500-pt corpus. argv[1] is the
// TIN fixture (points), argv[2] the contour/volume fixture.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "ingeneer/surface/contour.h"
#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;

namespace {

const char* g_points_path = nullptr;
const char* g_cv_path = nullptr;

double hexd(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

Tin ramp_grid() {
    // 5x5 unit grid, z = x: contours are vertical lines x = level.
    Tin tin;
    for (int y = 0; y <= 4; ++y) {
        for (int x = 0; x <= 4; ++x) {
            CHECK(tin.insert(x, y, x).has_value());
        }
    }
    return tin;
}

double polyline_length(const Contour& c) {
    double len = 0.0;
    const auto& p = c.points;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        len += std::hypot(p[i + 1].x - p[i].x, p[i + 1].y - p[i].y);
    }
    if (c.closed && p.size() > 1) {
        len += std::hypot(p.front().x - p.back().x, p.front().y - p.back().y);
    }
    return len;
}

void test_ramp_open_contour() {
    Tin tin = ramp_grid();
    auto r = extract_contours(tin, 1.5);
    CHECK(r.has_value());
    CHECK_EQ(r->contours.size(), static_cast<std::size_t>(1));
    const Contour& c = r->contours.front();
    CHECK(!c.closed);
    for (const auto& p : c.points) CHECK(p.x == 1.5);  // interpolation exact here
    // Hull-terminated: endpoints on the y = 0 / y = 4 boundary (either orientation).
    CHECK(std::fabs(c.points.front().y - c.points.back().y) == 4.0);
    CHECK(std::fabs(polyline_length(c) - 4.0) < 1e-12);
}

void test_closed_contour_around_peak() {
    Tin tin;
    CHECK(tin.insert(0, 0, 0).has_value());
    CHECK(tin.insert(4, 0, 0).has_value());
    CHECK(tin.insert(4, 4, 0).has_value());
    CHECK(tin.insert(0, 4, 0).has_value());
    CHECK(tin.insert(2, 2, 8).has_value());
    CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(4));

    auto r = extract_contours(tin, 4.0);
    CHECK(r.has_value());
    CHECK_EQ(r->contours.size(), static_cast<std::size_t>(1));
    const Contour& c = r->contours.front();
    CHECK(c.closed);
    CHECK_EQ(c.points.size(), static_cast<std::size_t>(4));
    // Midpoints of the four spokes: (1,1), (3,1), (3,3), (1,3) — exact interpolation.
    for (const auto& p : c.points) {
        CHECK((p.x == 1.0 || p.x == 3.0) && (p.y == 1.0 || p.y == 3.0));
    }
    CHECK(std::fabs(polyline_length(c) - 8.0) < 1e-12);  // closed contour closes
}

void test_vertex_on_level_degeneracy() {
    // Level 2.0 passes EXACTLY through the five vertices at x = 2 (z == level). Symbolic
    // perturbation (z+eps => above) must still yield one clean open polyline along x = 2,
    // its points being exactly the on-level vertices (duplicates squashed bit-exactly).
    Tin tin = ramp_grid();
    auto r = extract_contours(tin, 2.0);
    CHECK(r.has_value());
    CHECK_EQ(r->contours.size(), static_cast<std::size_t>(1));
    const Contour& c = r->contours.front();
    CHECK(!c.closed);
    CHECK_EQ(c.points.size(), static_cast<std::size_t>(5));
    std::vector<double> ys;
    for (const auto& p : c.points) {
        CHECK(p.x == 2.0);
        ys.push_back(p.y);
    }
    std::sort(ys.begin(), ys.end());
    for (int y = 0; y <= 4; ++y) CHECK(ys[static_cast<std::size_t>(y)] == y);
    CHECK(std::fabs(polyline_length(c) - 4.0) < 1e-12);
}

void test_levels_outside_surface() {
    Tin tin = ramp_grid();  // z in [0, 4]
    auto below = extract_contours(tin, -10.0);
    CHECK(below.has_value());  // empty result, not an error
    CHECK(below->contours.empty());
    auto above = extract_contours(tin, 100.0);
    CHECK(above.has_value());
    CHECK(above->contours.empty());

    auto nan = extract_contours(tin, std::nan(""));
    CHECK(!nan.has_value());
    if (!nan.has_value()) CHECK(nan.error().code == ContourErrc::NonFiniteLevel);
    auto inf = extract_contours(tin, HUGE_VAL);
    CHECK(!inf.has_value());
}

void test_chaikin_smoothing_is_derived() {
    static_assert(SmoothedContour::kDerived, "smoothed output must be tagged derived");
    Tin tin = ramp_grid();

    // Open: endpoints (hull termination) preserved exactly; point count grows.
    auto open = extract_contours(tin, 1.5);
    CHECK(open.has_value());
    const Contour& oc = open->contours.front();
    SmoothedContour so = chaikin_smooth(oc, 2);
    CHECK(!so.closed);
    CHECK(so.points.size() > oc.points.size());
    CHECK(so.points.front().x == oc.points.front().x);
    CHECK(so.points.front().y == oc.points.front().y);
    CHECK(so.points.back().x == oc.points.back().x);
    CHECK(so.points.back().y == oc.points.back().y);

    // Closed: wrap-around smoothing, 2x points per iteration.
    Tin peak;
    CHECK(peak.insert(0, 0, 0).has_value());
    CHECK(peak.insert(4, 0, 0).has_value());
    CHECK(peak.insert(4, 4, 0).has_value());
    CHECK(peak.insert(0, 4, 0).has_value());
    CHECK(peak.insert(2, 2, 8).has_value());
    auto closed = extract_contours(peak, 4.0);
    CHECK(closed.has_value());
    SmoothedContour sc = chaikin_smooth(closed->contours.front(), 1);
    CHECK(sc.closed);
    CHECK_EQ(sc.points.size(), static_cast<std::size_t>(8));
}

// --- oracle parity ----------------------------------------------------------------------

struct OracleLevel {
    double level = 0.0;
    double total_len = 0.0;
    std::vector<std::array<double, 4>> segs;  // canonical: endpoint-sorted, list-sorted
};

void canonicalize(std::vector<std::array<double, 4>>& segs) {
    for (auto& s : segs) {
        if (std::make_pair(s[2], s[3]) < std::make_pair(s[0], s[1])) {
            std::swap(s[0], s[2]);
            std::swap(s[1], s[3]);
        }
    }
    std::sort(segs.begin(), segs.end());
}

void test_oracle_parity() {
    std::ifstream pin(g_points_path);
    CHECK(static_cast<bool>(pin));
    std::vector<std::array<double, 3>> points;
    std::string line;
    while (std::getline(pin, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "points") {
            std::size_t n = 0;
            ss >> n;
            points.resize(n);
            for (auto& p : points) {
                std::string hx, hy, hz;
                pin >> hx >> hy >> hz;
                p = {hexd(hx), hexd(hy), hexd(hz)};
            }
            break;
        }
    }
    CHECK_EQ(points.size(), static_cast<std::size_t>(500));

    std::ifstream in(g_cv_path);
    CHECK(static_cast<bool>(in));
    std::vector<OracleLevel> levels;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "contour_level") {
            OracleLevel lv;
            std::string hz, hl;
            std::size_t nseg = 0;
            ss >> hz >> nseg >> hl;
            lv.level = hexd(hz);
            lv.total_len = hexd(hl);
            lv.segs.resize(nseg);
            for (auto& s : lv.segs) {
                std::string h0, h1, h2, h3;
                in >> h0 >> h1 >> h2 >> h3;
                s = {hexd(h0), hexd(h1), hexd(h2), hexd(h3)};
            }
            levels.push_back(std::move(lv));
        }
    }
    CHECK_EQ(levels.size(), static_cast<std::size_t>(3));

    Tin tin;
    for (const auto& p : points) CHECK(tin.insert(p[0], p[1], p[2]).has_value());

    for (const auto& lv : levels) {
        auto r = extract_contours(tin, lv.level);
        CHECK(r.has_value());

        // Decompose our polylines back into per-triangle segments.
        std::vector<std::array<double, 4>> mine;
        double total_len = 0.0;
        for (const Contour& c : r->contours) {
            const auto& p = c.points;
            const std::size_t n = p.size();
            const std::size_t edges = c.closed ? n : n - 1;
            for (std::size_t i = 0; i < edges; ++i) {
                const ContourPoint& a = p[i];
                const ContourPoint& b = p[(i + 1) % n];
                mine.push_back({a.x, a.y, b.x, b.y});
                total_len += std::hypot(b.x - a.x, b.y - a.y);
            }
        }
        CHECK_EQ(mine.size(), lv.segs.size());  // one segment per straddling triangle
        CHECK(std::fabs(total_len - lv.total_len) < 1e-2);
        if (mine.size() != lv.segs.size()) continue;

        canonicalize(mine);
        // Pointwise match within tolerance. The two implementations interpolate in
        // opposite edge directions (~1e-13 apart), which can swap the sort order of
        // adjacent entries sharing an endpoint — match within a small sliding window.
        std::vector<bool> used(mine.size(), false);
        std::size_t matched = 0;
        const std::size_t w = 8;
        for (std::size_t i = 0; i < lv.segs.size(); ++i) {
            const std::size_t lo = i > w ? i - w : 0;
            const std::size_t hi = std::min(mine.size(), i + w + 1);
            for (std::size_t j = lo; j < hi; ++j) {
                if (used[j]) continue;
                bool ok = true;
                for (int k = 0; k < 4; ++k) {
                    ok = ok && std::fabs(mine[j][static_cast<std::size_t>(k)] -
                                         lv.segs[i][static_cast<std::size_t>(k)]) < 1e-4;
                }
                if (ok) {
                    used[j] = true;
                    ++matched;
                    break;
                }
            }
        }
        CHECK_EQ(matched, lv.segs.size());
        std::printf("contour oracle parity: level %.1f, %zu segments, length %.4f\n", lv.level,
                    mine.size(), total_len);
    }
}

void run() {
    test_ramp_open_contour();
    test_closed_contour_around_peak();
    test_vertex_on_level_degeneracy();
    test_levels_outside_surface();
    test_chaikin_smoothing_is_derived();
    test_oracle_parity();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <tin_fixture> <cv_fixture>\n", argv[0]);
        return 2;
    }
    g_points_path = argv[1];
    g_cv_path = argv[2];
    run();
    return ::ingeneer::geom::test::g_failures == 0 ? 0 : 1;
}
