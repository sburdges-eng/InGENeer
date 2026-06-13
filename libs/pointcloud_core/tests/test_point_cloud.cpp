// SPDX-License-Identifier: Apache-2.0
//
// PointCloud container tests (Phase 7): append, bulk append (atomic), non-finite
// rejection, and the optional lazily-allocated attribute channels.

#include "ingeneer/pointcloud/point_cloud.h"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    // --- append + positions span -------------------------------------------------------
    {
        PointCloud pc;
        CHECK(pc.empty());
        auto a = pc.add_point(1.0F, 2.0F, 3.0F);
        CHECK(a.has_value());
        CHECK_EQ(*a, static_cast<PointId>(0));
        auto b = pc.add_point(Point3f{4.0F, 5.0F, 6.0F});
        CHECK(b.has_value());
        CHECK_EQ(*b, static_cast<PointId>(1));
        CHECK_EQ(pc.point_count(), static_cast<std::size_t>(2));

        auto span = pc.positions();
        CHECK_EQ(span.size(), static_cast<std::size_t>(2));
        CHECK_EQ(span[0].x, 1.0F);
        CHECK_EQ(span[1].z, 6.0F);
        CHECK_EQ(pc.position(1).y, 5.0F);
    }

    // --- non-finite rejection ----------------------------------------------------------
    {
        PointCloud pc;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        CHECK(!pc.add_point(nan, 0.0F, 0.0F).has_value());
        CHECK(!pc.add_point(0.0F, inf, 0.0F).has_value());
        CHECK_EQ(pc.add_point(nan, 0.0F, 0.0F).error().code, PointCloudErrc::NonFiniteCoordinate);
        CHECK_EQ(pc.point_count(), static_cast<std::size_t>(0));  // nothing appended
    }

    // --- bulk append is atomic ---------------------------------------------------------
    {
        PointCloud pc;
        std::array<Point3f, 3> ok{Point3f{0, 0, 0}, Point3f{1, 1, 1}, Point3f{2, 2, 2}};
        auto first = pc.add_points(ok);
        CHECK(first.has_value());
        CHECK_EQ(*first, static_cast<PointId>(0));
        CHECK_EQ(pc.point_count(), static_cast<std::size_t>(3));

        // Empty bulk append: no-op, returns kNoPoint sentinel.
        std::array<Point3f, 0> none{};
        auto e = pc.add_points(none);
        CHECK(e.has_value());
        CHECK_EQ(*e, kNoPoint);
        CHECK_EQ(pc.point_count(), static_cast<std::size_t>(3));

        // A non-finite anywhere in the batch rolls the whole batch back (atomic).
        const float inf = std::numeric_limits<float>::infinity();
        std::array<Point3f, 3> bad{Point3f{3, 3, 3}, Point3f{inf, 0, 0}, Point3f{4, 4, 4}};
        auto r = pc.add_points(bad);
        CHECK(!r.has_value());
        CHECK_EQ(r.error().code, PointCloudErrc::NonFiniteCoordinate);
        CHECK_EQ(pc.point_count(), static_cast<std::size_t>(3));  // unchanged
    }

    // --- optional attribute channels (lazy, dense, point_count-long) -------------------
    {
        PointCloud pc;
        for (int i = 0; i < 4; ++i) {
            (void)pc.add_point(static_cast<float>(i), 0.0F, 0.0F);
        }
        CHECK(!pc.has_intensity());
        CHECK_EQ(pc.intensity(0).has_value(), false);

        auto si = pc.set_intensity(2, 4242);
        CHECK(si.has_value());
        CHECK(pc.has_intensity());
        // Channel allocated dense over all 4 points; unset slots default to 0.
        CHECK_EQ(pc.intensity(0).value(), static_cast<std::uint16_t>(0));
        CHECK_EQ(pc.intensity(2).value(), static_cast<std::uint16_t>(4242));

        // A point appended AFTER channel allocation grows the channel.
        (void)pc.add_point(99.0F, 0.0F, 0.0F);
        CHECK_EQ(pc.point_count(), static_cast<std::size_t>(5));
        CHECK_EQ(pc.intensity(4).value(), static_cast<std::uint16_t>(0));

        CHECK(pc.set_classification(0, 2).has_value());
        CHECK_EQ(pc.classification(0).value(), static_cast<std::uint8_t>(2));
        CHECK(pc.set_return_number(0, 1).has_value());
        CHECK_EQ(pc.return_number(0).value(), static_cast<std::uint8_t>(1));
        CHECK(pc.set_rgb(0, 10, 20, 30).has_value());
        auto rgb = pc.rgb(0);
        CHECK(rgb.has_value());
        CHECK_EQ((*rgb)[0], static_cast<std::uint16_t>(10));
        CHECK_EQ((*rgb)[2], static_cast<std::uint16_t>(30));

        // Out-of-range PointId is rejected, not silently ignored.
        auto oor = pc.set_intensity(99, 1);
        CHECK(!oor.has_value());
        CHECK_EQ(oor.error().code, PointCloudErrc::PointIdOutOfRange);
    }

    // --- clear() drops points and channels ---------------------------------------------
    {
        PointCloud pc;
        (void)pc.add_point(1, 2, 3);
        (void)pc.set_intensity(0, 7);
        pc.clear();
        CHECK(pc.empty());
        CHECK(!pc.has_intensity());
    }
}

TEST_MAIN_RUN()
