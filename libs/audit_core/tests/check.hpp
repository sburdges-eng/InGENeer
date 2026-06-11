// Minimal dependency-free test harness for audit_core (no GoogleTest/Catch vendoring yet;
// keeps Phase 2.2 license-clean and offline). Each test is an executable returning 0 on
// pass, non-zero on failure; CTest aggregates. Replace with a vendored framework only when
// a phase justifies the dependency.
#ifndef INGENEER_AUDIT_TEST_CHECK_HPP
#define INGENEER_AUDIT_TEST_CHECK_HPP

#include <cstdio>
#include <string>

namespace ingeneer::audit::test {
inline int g_failures = 0;
}

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #cond, __FILE__, __LINE__); \
            ++::ingeneer::audit::test::g_failures;                                             \
        }                                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        auto _va = (a);                                                                       \
        auto _vb = (b);                                                                       \
        if (!(_va == _vb)) {                                                                  \
            std::fprintf(stderr, "CHECK_EQ failed: %s == %s\n  at %s:%d\n", #a, #b, __FILE__, \
                         __LINE__);                                                           \
            ++::ingeneer::audit::test::g_failures;                                            \
        }                                                                                     \
    } while (0)

#define TEST_MAIN() \
    int main() { return ::ingeneer::audit::test::g_failures == 0 ? 0 : 1; }

// Defines main(), calls the file-local run(), returns pass/fail.
#define TEST_MAIN_RUN()                                          \
    int main() {                                                 \
        run();                                                   \
        return ::ingeneer::audit::test::g_failures == 0 ? 0 : 1; \
    }

#endif  // INGENEER_AUDIT_TEST_CHECK_HPP
