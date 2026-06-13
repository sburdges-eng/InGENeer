// R-4.4 recompute invariant ("CRS changes MUST force recomputation"; carried TOTaLi
// invariant) — explicit assertion of the load-bearing deliverable.
//
// This test uses ONLY the pure CrsStamped value logic plus make_crs for identity, so it
// is the determinism-relevant half of the module and instruments cleanly under every
// sanitizer (no heavy PROJ transform pipeline involved).
#include <string>

#include "check.hpp"
#include "ingeneer/coord/crs.h"
#include "ingeneer/coord/crs_stamp.h"

using namespace ingeneer::coord;

namespace {

// A stand-in "derived/cached result" (e.g. a survey computation produced under a CRS).
struct SurveyResult {
    double value = 0.0;
};

void run_impl() {
    auto crs_a = make_crs("EPSG:4326");   // WGS84 geographic
    auto crs_b = make_crs("EPSG:6340");   // NAD83(2011) / UTM 14N (US, projected)
    auto crs_a2 = make_crs("EPSG:4326");  // same authority code, re-stated
    CHECK(crs_a.has_value());
    CHECK(crs_b.has_value());
    CHECK(crs_a2.has_value());

    // Identity: equivalent definitions compare equal; different CRS compare unequal.
    CHECK(*crs_a == *crs_a2);
    CHECK(*crs_a != *crs_b);

    // Compute a result UNDER crs A and stamp it.
    CrsStamped<SurveyResult> cache = stamp(*crs_a, SurveyResult{42.0});

    // Under the SAME CRS: no recompute, value readable.
    CHECK(!cache.needs_recompute(*crs_a));
    CHECK(cache.value_for(*crs_a).has_value());
    CHECK_EQ(cache.value_for(*crs_a)->get().value, 42.0);

    // Re-stating the original CRS via a fresh equal Crs object: STILL no recompute
    // (identity is by definition, not by object).
    CHECK(!cache.needs_recompute(*crs_a2));
    CHECK(cache.value_for(*crs_a2).has_value());

    // CRS CHANGE to B: MUST force recompute, and the checked accessor MUST refuse the
    // stale value (returns nullopt). This is the R-4.4 invariant.
    CHECK(cache.needs_recompute(*crs_b));
    CHECK(!cache.value_for(*crs_b).has_value());

    // Forced recompute under B re-binds the stamp; now B is current, A is stale.
    cache = stamp(*crs_b, SurveyResult{99.0});
    CHECK(!cache.needs_recompute(*crs_b));
    CHECK(cache.value_for(*crs_b).has_value());
    CHECK_EQ(cache.value_for(*crs_b)->get().value, 99.0);
    CHECK(cache.needs_recompute(*crs_a));  // switching back to A is again stale
    CHECK(!cache.value_for(*crs_a).has_value());
}

}  // namespace

static void run() { run_impl(); }

TEST_MAIN_RUN()
