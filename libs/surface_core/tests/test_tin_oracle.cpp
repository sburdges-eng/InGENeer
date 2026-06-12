// surface_core TIN — TOTaLi oracle parity (ADR-0023; Phase 6 exit gate component).
//
// Loads the frozen fixture extracted from TOTaLi's survey corpus (scipy/Qhull Delaunay
// semantics), triangulates the same 500 points, and requires:
//   * identical triangle COUNT and identical canonical triangle SET (the corpus has no
//     cocircular ties, so the Delaunay triangulation is unique);
//   * identical hull size;
//   * total 2D area within the fixture's stated tolerance (area_m2 = 1e-2).
// Fixture path is argv[1] (wired by CMake).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;

namespace {
const char* g_fixture_path = nullptr;
}

static void run() {
    std::ifstream in(g_fixture_path);
    CHECK(static_cast<bool>(in));

    std::string line;
    std::vector<std::array<double, 3>> points;
    std::vector<std::array<VertexId, 3>> oracle_tris;
    std::size_t hull_size = 0;
    double total_area = 0.0;

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "points") {
            std::size_t n = 0;
            ss >> n;
            points.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                std::string hx, hy, hz;
                in >> hx >> hy >> hz;
                points[i] = {std::strtod(hx.c_str(), nullptr), std::strtod(hy.c_str(), nullptr),
                             std::strtod(hz.c_str(), nullptr)};
            }
        } else if (tag == "triangles") {
            std::size_t t = 0;
            ss >> t;
            oracle_tris.resize(t);
            for (std::size_t i = 0; i < t; ++i) {
                in >> oracle_tris[i][0] >> oracle_tris[i][1] >> oracle_tris[i][2];
            }
        } else if (tag == "hull_size") {
            ss >> hull_size;
        } else if (tag == "total_area") {
            std::string hex;
            ss >> hex;
            total_area = std::strtod(hex.c_str(), nullptr);
        }
    }
    CHECK_EQ(points.size(), static_cast<std::size_t>(500));
    CHECK_EQ(oracle_tris.size(), static_cast<std::size_t>(980));

    Tin tin;
    for (const auto& p : points) {
        CHECK(tin.insert(p[0], p[1], p[2]).has_value());
    }
    CHECK_EQ(tin.vertex_count(), points.size());
    CHECK_EQ(tin.triangle_count(), oracle_tris.size());
    CHECK_EQ(tin.hull_size(), hull_size);

    // Canonical triangle-set equality.
    auto ours = tin.triangles();
    for (auto& t : ours) std::sort(t.begin(), t.end());
    std::sort(ours.begin(), ours.end());
    // Fixture triangles are already canonical (sorted within, sorted list).
    CHECK(ours == oracle_tris);

    // Area parity within the stated tolerance.
    double area = 0.0;
    for (const auto& t : ours) {
        const auto& a = tin.vertex(t[0]);
        const auto& b = tin.vertex(t[1]);
        const auto& c = tin.vertex(t[2]);
        area += std::fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5;
    }
    CHECK(std::fabs(area - total_area) < 1e-2);
    std::printf("oracle parity: %zu triangles, hull %zu, area %.4f (oracle %.4f)\n", ours.size(),
                hull_size, area, total_area);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <fixture>\n", argv[0]);
        return 2;
    }
    g_fixture_path = argv[1];
    run();
    return ::ingeneer::geom::test::g_failures == 0 ? 0 : 1;
}
