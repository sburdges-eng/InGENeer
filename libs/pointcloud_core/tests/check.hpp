// Minimal dependency-free test harness for pointcloud_core (same pattern as
// libs/geometry_core/tests/check.hpp and libs/surface_core/tests/check.hpp; no
// GoogleTest/Catch vendoring — keeps the phase license-clean and offline). Each test is an
// executable returning 0 on pass, non-zero on failure; CTest aggregates.
#ifndef INGENEER_POINTCLOUD_TEST_CHECK_HPP
#define INGENEER_POINTCLOUD_TEST_CHECK_HPP

#include <cstdio>

namespace ingeneer::pointcloud::test {
inline int g_failures = 0;
}

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #cond, __FILE__, __LINE__); \
            ++::ingeneer::pointcloud::test::g_failures;                                        \
        }                                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        auto _va = (a);                                                                       \
        auto _vb = (b);                                                                       \
        if (!(_va == _vb)) {                                                                  \
            std::fprintf(stderr, "CHECK_EQ failed: %s == %s\n  at %s:%d\n", #a, #b, __FILE__, \
                         __LINE__);                                                           \
            ++::ingeneer::pointcloud::test::g_failures;                                       \
        }                                                                                     \
    } while (0)

#define TEST_MAIN_RUN()                                               \
    int main() {                                                      \
        run();                                                        \
        return ::ingeneer::pointcloud::test::g_failures == 0 ? 0 : 1; \
    }

#endif  // INGENEER_POINTCLOUD_TEST_CHECK_HPP
