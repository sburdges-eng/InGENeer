// PROJ-backed transform behavior. CRS chosen to exercise the no-US-hardcoding property:
// a US projected CRS, a European projected CRS, and a non-CRS string error case.
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "check.hpp"
#include "ingeneer/coord/crs.h"

using namespace ingeneer::coord;

namespace {

void run_impl() {
    // Provenance: PROJ version is reported (audit/oracle pin point).
    CHECK(!proj_version().empty());

    // --- round-trip through a US projected CRS (EPSG:6340, NAD83(2011)/UTM 14N) -------
    {
        auto geo = make_crs("EPSG:4326");  // WGS84 lon/lat (visualization order)
        auto utm = make_crs("EPSG:6340");
        CHECK(geo.has_value());
        CHECK(utm.has_value());

        auto fwd = Transform::create(*geo, *utm);
        auto inv = Transform::create(*utm, *geo);
        CHECK(fwd.has_value());
        CHECK(inv.has_value());

        // A point well inside UTM zone 14N (central US): lon -99, lat 40.
        const Coordinate in{-99.0, 40.0, 0.0};
        auto proj = fwd->forward(in);
        CHECK(proj.has_value());
        auto back = inv->forward(*proj);
        CHECK(back.has_value());
        CHECK_NEAR(back->x, in.x, 1e-7);  // degrees
        CHECK_NEAR(back->y, in.y, 1e-7);
    }

    // --- non-US CRS (EPSG:25832, ETRS89 / UTM 32N — central Europe) -------------------
    // Proves the layer carries zero US assumptions: a European projected CRS transforms
    // round-trip identically through the same generic API.
    {
        auto geo = make_crs("EPSG:4326");
        auto utm32 = make_crs("EPSG:25832");
        CHECK(geo.has_value());
        CHECK(utm32.has_value());

        auto fwd = Transform::create(*geo, *utm32);
        auto inv = Transform::create(*utm32, *geo);
        CHECK(fwd.has_value());
        CHECK(inv.has_value());

        const Coordinate in{9.0, 50.0, 0.0};  // lon 9E, lat 50N (Germany)
        auto proj = fwd->forward(in);
        CHECK(proj.has_value());
        // Sanity: easting should be near the 500 km false-easting of a UTM central zone.
        CHECK(proj->x > 100000.0 && proj->x < 900000.0);
        auto back = inv->forward(*proj);
        CHECK(back.has_value());
        CHECK_NEAR(back->x, in.x, 1e-7);
        CHECK_NEAR(back->y, in.y, 1e-7);
    }

    // --- batched span transform preserves order/count --------------------------------
    {
        auto geo = make_crs("EPSG:4326");
        auto utm = make_crs("EPSG:6340");
        CHECK(geo.has_value());
        CHECK(utm.has_value());
        auto fwd = Transform::create(*geo, *utm);
        CHECK(fwd.has_value());

        std::array<Coordinate, 3> pts{
            Coordinate{-99.0, 40.0, 0.0},
            Coordinate{-98.5, 40.5, 10.0},
            Coordinate{-99.5, 39.5, -5.0},
        };
        auto batch = fwd->forward(std::span<const Coordinate>(pts));
        CHECK(batch.has_value());
        CHECK_EQ(batch->size(), static_cast<std::size_t>(3));
        // Each batched result matches the single-point path exactly (determinism).
        for (std::size_t i = 0; i < pts.size(); ++i) {
            auto single = fwd->forward(pts[i]);
            CHECK(single.has_value());
            CHECK(((*batch)[i] == *single));
        }
    }

    // --- invalid CRS string -> error, not crash --------------------------------------
    {
        auto bad = make_crs("EPSG:NOT_A_REAL_CODE_12345");
        CHECK(!bad.has_value());
        CHECK(bad.error().code == CrsErrc::InvalidDefinition);

        auto garbage = make_crs("this is not a crs at all");
        CHECK(!garbage.has_value());

        auto empty = make_crs("");
        CHECK(!empty.has_value());
        CHECK(empty.error().code == CrsErrc::InvalidDefinition);
    }

    // --- unset CRS as transform endpoint -> error ------------------------------------
    {
        Crs unset;
        auto good = make_crs("EPSG:4326");
        CHECK(good.has_value());
        auto t = Transform::create(unset, *good);
        CHECK(!t.has_value());
        CHECK(t.error().code == CrsErrc::InvalidDefinition);
    }

    // --- non-finite input -> NonFiniteInput error ------------------------------------
    {
        auto geo = make_crs("EPSG:4326");
        auto utm = make_crs("EPSG:6340");
        CHECK(geo.has_value());
        CHECK(utm.has_value());
        auto fwd = Transform::create(*geo, *utm);
        CHECK(fwd.has_value());

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        auto r_nan = fwd->forward(Coordinate{nan, 40.0, 0.0});
        CHECK(!r_nan.has_value());
        CHECK(r_nan.error().code == CrsErrc::NonFiniteInput);
        auto r_inf = fwd->forward(Coordinate{-99.0, inf, 0.0});
        CHECK(!r_inf.has_value());
        CHECK(r_inf.error().code == CrsErrc::NonFiniteInput);
    }
}

}  // namespace

static void run() { run_impl(); }

TEST_MAIN_RUN()
