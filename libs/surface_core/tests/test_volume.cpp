// surface_core volumes — analytic known answers + pinned numpy cross-check (Phase 6.3).
//
// TIN-prism vs hand-computed tetrahedron / pyramid, exact mixed-triangle cut/fill
// splitting, signed cut-vs-fill convention, shared-support surface-to-surface volumes,
// prismoidal vs analytic cross-sections (average-end-area as labeled report option).
//
// Oracle: TOTaLi has no volume pipeline (groundtruthos-data/pipeline/features.py only
// reads precomputed metadata), so per ADR-0023 the corpus-scale quantities are a
// scipy/numpy cross-check pinned by tools/oracle/extract_from_totali.py into the cv
// fixture. argv[1] is the TIN fixture (points), argv[2] the contour/volume fixture.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "ingeneer/surface/tin.h"
#include "ingeneer/surface/volume.h"

using namespace ingeneer::surface;

namespace {

const char* g_points_path = nullptr;
const char* g_cv_path = nullptr;

double hexd(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

// Square pyramid: 2x2 base at z = 0, apex (1,1,3). Footprint area 4, volume 4.
Tin pyramid() {
    Tin tin;
    CHECK(tin.insert(0, 0, 0).has_value());
    CHECK(tin.insert(2, 0, 0).has_value());
    CHECK(tin.insert(2, 2, 0).has_value());
    CHECK(tin.insert(0, 2, 0).has_value());
    CHECK(tin.insert(1, 1, 3).has_value());
    CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(4));
    return tin;
}

void test_tetra_prism() {
    // Single triangle (area 1/2) with one vertex at z = 3: the solid above z = 0 is a
    // tetrahedron, V = base * h / 3 = (1/2)(3)/3 = 1/2... computed per prism formula
    // A * (d1+d2+d3)/3 = 0.5 * (3+0+0)/3 = 0.5. Hand-computed.
    Tin tin;
    CHECK(tin.insert(0, 0, 3).has_value());
    CHECK(tin.insert(1, 0, 0).has_value());
    CHECK(tin.insert(0, 1, 0).has_value());
    auto r = volume_to_plane(tin, 0.0);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 0.5) < 1e-12);
    CHECK(r->fill == 0.0);
}

void test_pyramid_prism_and_mixed_split() {
    Tin tin = pyramid();

    // Whole pyramid above its base plane: V = 4 * 3 / 3 = 4. All cut.
    auto base = volume_to_plane(tin, 0.0);
    CHECK(base.has_value());
    CHECK(std::fabs(base->cut - 4.0) < 1e-12);
    CHECK(base->fill == 0.0);

    // Plane z = 1 slices every fan triangle (mixed signs): cut is the similar pyramid
    // above the plane, scale 2/3 => V = 4 * (2/3)^3 = 32/27; net = 4 - 1*4 = 0 so
    // fill = cut. Exercises the exact d = 0 clipping.
    auto mid = volume_to_plane(tin, 1.0);
    CHECK(mid.has_value());
    CHECK(std::fabs(mid->cut - 32.0 / 27.0) < 1e-12);
    CHECK(std::fabs(mid->fill - 32.0 / 27.0) < 1e-12);
    CHECK(std::fabs(mid->net()) < 1e-12);

    // Plane through the apex: nothing above it (apex d == 0 contributes no volume).
    auto top = volume_to_plane(tin, 3.0);
    CHECK(top.has_value());
    CHECK(top->cut == 0.0);
    CHECK(std::fabs(top->fill - (3.0 * 4.0 - 4.0)) < 1e-12);

    auto nan = volume_to_plane(tin, std::nan(""));
    CHECK(!nan.has_value());
    if (!nan.has_value()) CHECK(nan.error().code == VolumeErrc::NonFiniteInput);
}

void test_sign_convention() {
    Tin tin = pyramid();  // z in [0, 3], footprint area 4, volume 4

    // Surface entirely ABOVE the reference plane => cut only, net positive (excavation).
    auto below = volume_to_plane(tin, -2.0);
    CHECK(below.has_value());
    CHECK(std::fabs(below->cut - (2.0 * 4.0 + 4.0)) < 1e-12);
    CHECK(below->fill == 0.0);
    CHECK(below->net() > 0.0);

    // Surface entirely BELOW the reference plane => fill only, net negative.
    auto above = volume_to_plane(tin, 10.0);
    CHECK(above.has_value());
    CHECK(above->cut == 0.0);
    CHECK(std::fabs(above->fill - (10.0 * 4.0 - 4.0)) < 1e-12);
    CHECK(above->net() < 0.0);
}

void test_surface_to_surface_shared_support() {
    Tin design = pyramid();

    // Existing = design raised by 0.5 everywhere: cut = 0.5 * footprint = 2, fill = 0.
    Tin raised;
    CHECK(raised.insert(0, 0, 0.5).has_value());
    CHECK(raised.insert(2, 0, 0.5).has_value());
    CHECK(raised.insert(2, 2, 0.5).has_value());
    CHECK(raised.insert(0, 2, 0.5).has_value());
    CHECK(raised.insert(1, 1, 3.5).has_value());
    auto r = volume_between(design, raised);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 2.0) < 1e-12);
    CHECK(r->fill == 0.0);

    // Swapped roles: existing below design => fill.
    auto swapped = volume_between(raised, design);
    CHECK(swapped.has_value());
    CHECK(swapped->cut == 0.0);
    CHECK(std::fabs(swapped->fill - 2.0) < 1e-12);

    // Unshared support is rejected (independent-triangulation merge is deferred).
    Tin other;
    CHECK(other.insert(0, 0, 0).has_value());
    CHECK(other.insert(3, 0, 0).has_value());  // different xy support
    CHECK(other.insert(2, 2, 0).has_value());
    CHECK(other.insert(0, 2, 0).has_value());
    CHECK(other.insert(1, 1, 3).has_value());
    auto bad = volume_between(design, other);
    CHECK(!bad.has_value());
    if (!bad.has_value()) CHECK(bad.error().code == VolumeErrc::UnsharedSupport);

    Tin fewer;
    CHECK(fewer.insert(0, 0, 0).has_value());
    CHECK(fewer.insert(2, 0, 0).has_value());
    CHECK(fewer.insert(0, 2, 0).has_value());
    auto bad2 = volume_between(design, fewer);
    CHECK(!bad2.has_value());
    if (!bad2.has_value()) CHECK(bad2.error().code == VolumeErrc::UnsharedSupport);
}

void test_mixed_triangle_exact_split() {
    // Single triangle, d = (-1, -1, 3) vs plane 0; plan area 8. Hand computation:
    // positive sub-triangle (3,1)-(0,4)-(0,1), area 4.5 => cut = 4.5 * (3/3) = 4.5;
    // signed prism = 8 * (−1−1+3)/3 = 8/3; fill = cut − net = 4.5 − 8/3 = 11/6.
    Tin tin;
    CHECK(tin.insert(0, 0, -1).has_value());
    CHECK(tin.insert(4, 0, -1).has_value());
    CHECK(tin.insert(0, 4, 3).has_value());
    auto r = volume_to_plane(tin, 0.0);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - 4.5) < 1e-12);
    CHECK(std::fabs(r->fill - 11.0 / 6.0) < 1e-12);
    CHECK(std::fabs(r->net() - 8.0 / 3.0) < 1e-12);
}

void test_prismoidal_vs_analytic() {
    // Pyramid lying on its side: cross-section area A(x) = x^2 over x in [0, 6];
    // V = integral = 72 exactly. Prismoidal is exact for quadratic area variation:
    // L/6 (A1 + 4 Am + A2) = 6/6 (0 + 4*9 + 36) = 72.
    CHECK(prismoidal_volume(0.0, 9.0, 36.0, 6.0) == 72.0);
    // Average end area (labeled report option) overestimates the same solid: 108.
    CHECK(average_end_area_volume(0.0, 36.0, 6.0) == 108.0);
    // Constant cross-section (a prism): both methods agree exactly.
    CHECK(prismoidal_volume(5.0, 5.0, 5.0, 2.0) == 10.0);
    CHECK(average_end_area_volume(5.0, 5.0, 2.0) == 10.0);
}

void test_oracle_cross_check() {
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

    double plane_z = 0.0, oracle_cut = 0.0, oracle_fill = 0.0;
    bool found = false;
    std::ifstream in(g_cv_path);
    CHECK(static_cast<bool>(in));
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "volume_plane") {
            std::string hz, hc, hf;
            ss >> hz >> hc >> hf;
            plane_z = hexd(hz);
            oracle_cut = hexd(hc);
            oracle_fill = hexd(hf);
            found = true;
        }
    }
    CHECK(found);

    Tin tin;
    for (const auto& p : points) CHECK(tin.insert(p[0], p[1], p[2]).has_value());
    auto r = volume_to_plane(tin, plane_z);
    CHECK(r.has_value());
    CHECK(std::fabs(r->cut - oracle_cut) < 1e-1);  // volume_m3 tolerance
    CHECK(std::fabs(r->fill - oracle_fill) < 1e-1);
    std::printf(
        "volume oracle cross-check: plane %.1f cut %.4f (oracle %.4f) fill %.4f "
        "(oracle %.4f)\n",
        plane_z, r->cut, oracle_cut, r->fill, oracle_fill);
}

void run() {
    test_tetra_prism();
    test_pyramid_prism_and_mixed_split();
    test_sign_convention();
    test_surface_to_surface_shared_support();
    test_mixed_triangle_exact_split();
    test_prismoidal_vs_analytic();
    test_oracle_cross_check();
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
