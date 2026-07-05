// Minimal dependency-free test harness for coordinate_core (same pattern as
// libs/geometry_core/tests/check.hpp and libs/surface_core/tests/check.hpp; no
// GoogleTest/Catch vendoring — keeps the phase license-clean and offline). Each test is
// an executable returning 0 on pass, non-zero on failure; CTest aggregates.
#ifndef INGENEER_COORD_TEST_CHECK_HPP
#define INGENEER_COORD_TEST_CHECK_HPP

#include <cmath>
#include <cstdio>

namespace ingeneer::coord::test {
inline int g_failures = 0;
}

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #cond, __FILE__, __LINE__); \
            ++::ingeneer::coord::test::g_failures;                                             \
        }                                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                       \
    do {                                                                                     \
        auto _va = (a);                                                                      \
        auto _vb = (b);                                                                      \
        if (!(_va == _vb)) {                                                                 \
            std::fprintf(stderr, "CHECK_EQ failed: %s == %s\n at %s:%d\n", #a, #b, __FILE__, \
                         __LINE__);                                                          \
            ++::ingeneer::coord::test::g_failures;                                           \
        }                                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                      \
    do {                                                                                           \
        const double _da = (a);                                                                    \
        const double _db = (b);                                                                    \
        const double _dt = (tol);                                                                  \
        if (!(std::fabs(_da - _db) <= _dt)) {                                                      \
            std::fprintf(stderr,                                                                   \
                         "CHECK_NEAR failed: |%s - %s| <= %s  (%.10g vs %.10g)\n  at %s:%d\n", #a, \
                         #b, #tol, _da, _db, __FILE__, __LINE__);                                  \
            ++::ingeneer::coord::test::g_failures;                                                 \
        }                                                                                          \
    } while (0)

#define TEST_MAIN_RUN()                                          \
    int main() {                                                 \
        run();                                                   \
        return ::ingeneer::coord::test::g_failures == 0 ? 0 : 1; \
    }

#endif  // INGENEER_COORD_TEST_CHECK_HPP
