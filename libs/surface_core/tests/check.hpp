// Minimal dependency-free test harness for geometry_core (same pattern as
// libs/audit_core/tests/check.hpp; no GoogleTest/Catch vendoring — keeps the phase
// license-clean and offline). Each test is an executable returning 0 on pass, non-zero on
// failure; CTest aggregates. Replace with a vendored framework only when a phase justifies
// the dependency.
#ifndef INGENEER_GEOM_TEST_CHECK_HPP
#define INGENEER_GEOM_TEST_CHECK_HPP

#include <cstdio>

namespace ingeneer::geom::test {
inline int g_failures = 0;
}

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #cond, __FILE__, __LINE__); \
            ++::ingeneer::geom::test::g_failures;                                              \
        }                                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        auto _va = (a);                                                                       \
        auto _vb = (b);                                                                       \
        if (!(_va == _vb)) {                                                                  \
            std::fprintf(stderr, "CHECK_EQ failed: %s == %s\n  at %s:%d\n", #a, #b, __FILE__, \
                         __LINE__);                                                           \
            ++::ingeneer::geom::test::g_failures;                                             \
        }                                                                                     \
    } while (0)

#define TEST_MAIN() \
    int main() { return ::ingeneer::geom::test::g_failures == 0 ? 0 : 1; }

// Defines main(), calls the file-local run(), returns pass/fail.
#define TEST_MAIN_RUN()                                         \
    int main() {                                                \
        run();                                                  \
        return ::ingeneer::geom::test::g_failures == 0 ? 0 : 1; \
    }

#endif  // INGENEER_GEOM_TEST_CHECK_HPP
