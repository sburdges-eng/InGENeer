// Phase 5.3 smoke test: the vendored CDT (MPL-2.0) and nanoflann (BSD) libraries compile,
// link, and produce sane results when consumed from geometry_core's test target. This is
// the integration proof for third_party/ vendoring — the full wrapper API (std::expected
// errors, index handles) lands with the first real consumer (surface_core, Phase 6).
#include <CDT.h>
#include <nanoflann.hpp>

#include <array>
#include <cstddef>
#include <vector>

#include "check.hpp"

namespace {

// Minimal nanoflann point-cloud adaptor over a flat xy vector.
struct Cloud {
    std::vector<std::array<double, 2>> pts;
    std::size_t kdtree_get_point_count() const { return pts.size(); }
    double kdtree_get_pt(std::size_t i, std::size_t dim) const { return pts[i][dim]; }
    template <class BBox>
    bool kdtree_get_bbox(BBox&) const {
        return false;
    }
};

}  // namespace

static void run() {
    // --- CDT: constrained Delaunay triangulation of a unit square + center point -------
    CDT::Triangulation<double> cdt;
    cdt.insertVertices(
        std::vector<CDT::V2d<double>>{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}, {0.5, 0.5}});
    cdt.eraseSuperTriangle();
    // A square with a center vertex triangulates into exactly 4 triangles.
    CHECK_EQ(cdt.triangles.size(), static_cast<std::size_t>(4));
    CHECK_EQ(cdt.vertices.size(), static_cast<std::size_t>(5));

    // --- nanoflann: KD-tree nearest-neighbour over a small grid ------------------------
    Cloud cloud;
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) {
            cloud.pts.push_back({static_cast<double>(x), static_cast<double>(y)});
        }
    }
    using KdTree =
        nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<double, Cloud>, Cloud, 2>;
    KdTree tree(2, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));

    const double query[2] = {3.2, 6.9};
    std::size_t idx = 0;
    double dist_sq = 0.0;
    nanoflann::KNNResultSet<double> result(1);
    result.init(&idx, &dist_sq);
    tree.findNeighbors(result, query);
    // Nearest grid point to (3.2, 6.9) is (3, 7).
    CHECK_EQ(cloud.pts[idx][0], 3.0);
    CHECK_EQ(cloud.pts[idx][1], 7.0);
}

TEST_MAIN_RUN()
