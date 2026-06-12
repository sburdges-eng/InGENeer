// surface_core TIN — basic known-answer cases (Phase 6.1 TDD).
#include <cmath>
#include <limits>

#include "check.hpp"
#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;

static void run() {
    // --- square + center: exactly 4 triangles, hull 4 ----------------------------------
    {
        Tin tin;
        CHECK(tin.insert(0, 0, 1).has_value());
        CHECK(tin.insert(1, 0, 2).has_value());
        CHECK(tin.insert(1, 1, 3).has_value());
        CHECK(tin.insert(0, 1, 4).has_value());
        CHECK(tin.insert(0.5, 0.5, 5).has_value());
        CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(4));
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(4));
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(5));
    }

    // --- 3x3 grid: T = 2n - 2 - h = 18 - 2 - 8 = 8 --------------------------------------
    {
        Tin tin;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                CHECK(tin.insert(x, y, x + y).has_value());
            }
        }
        CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(8));
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(8));
    }

    // --- all-collinear input: zero finite triangles -------------------------------------
    {
        Tin tin;
        for (int i = 0; i < 6; ++i) {
            CHECK(tin.insert(i, 2 * i, 0).has_value());
        }
        CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(0));
    }

    // --- collinear prefix, then one off-line point: full fan ----------------------------
    {
        Tin tin;
        for (int i = 0; i < 5; ++i) {
            CHECK(tin.insert(i, 0, 0).has_value());
        }
        CHECK(tin.insert(2, 3, 1).has_value());  // breaks collinearity
        // 6 points, 5 on the hull bottom edge... hull = all except none: n=6, interior 0.
        // Hull vertices: (0,0),(4,0),(2,3) corners + the 3 mid points lie ON the hull
        // boundary -> hull edges h = 6; T = 2*6 - 2 - 6 = 4.
        CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(4));
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(6));
    }

    // --- duplicates return the original id; z ignored ----------------------------------
    {
        Tin tin;
        auto a = tin.insert(0, 0, 1);
        CHECK(a.has_value());
        CHECK(tin.insert(2, 0, 1).has_value());
        CHECK(tin.insert(0, 2, 1).has_value());
        auto dup = tin.insert(0, 0, 99);
        CHECK(dup.has_value());
        CHECK_EQ(*dup, *a);
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(3));
        // duplicate of an interior point after more inserts
        CHECK(tin.insert(0.5, 0.5, 0).has_value());
        auto dup2 = tin.insert(0.5, 0.5, 42);
        CHECK(dup2.has_value());
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(4));
    }

    // --- point exactly ON a hull edge joins the hull ------------------------------------
    {
        Tin tin;
        CHECK(tin.insert(0, 0, 0).has_value());
        CHECK(tin.insert(4, 0, 0).has_value());
        CHECK(tin.insert(2, 4, 0).has_value());
        CHECK(tin.insert(2, 0, 0).has_value());  // midpoint of bottom hull edge
        CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(2));
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(4));
    }

    // --- point outside the hull (hull growth) -------------------------------------------
    {
        Tin tin;
        CHECK(tin.insert(0, 0, 0).has_value());
        CHECK(tin.insert(2, 0, 0).has_value());
        CHECK(tin.insert(1, 2, 0).has_value());
        CHECK(tin.insert(1, -3, 0).has_value());  // below the bottom edge
        CHECK_EQ(tin.triangle_count(), static_cast<std::size_t>(2));
        CHECK_EQ(tin.hull_size(), static_cast<std::size_t>(4));
        CHECK(tin.insert(5, 5, 0).has_value());  // beyond a corner
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(5));
    }

    // --- non-finite coordinates rejected -------------------------------------------------
    {
        Tin tin;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        CHECK(!tin.insert(nan, 0, 0).has_value());
        CHECK(!tin.insert(0, inf, 0).has_value());
        CHECK(!tin.insert(0, 0, -inf).has_value());
        CHECK_EQ(tin.vertex_count(), static_cast<std::size_t>(0));
    }
}

TEST_MAIN_RUN()
