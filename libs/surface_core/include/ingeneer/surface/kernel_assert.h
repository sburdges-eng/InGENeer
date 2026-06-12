// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/surface/kernel_assert.h — tiered kernel assertions (plan §3.4, Phase 6.5).
//
// surface_core-local: geometry_core is consumed strictly read-only by this phase, so the
// assertion macros live here instead of a shared geometry header (noted in the Phase 6.5
// session record; promote to a shared header when a second engine library needs them).
//
// Tiers:
//   * KERNEL_ASSERT(cond, msg)       — ALWAYS ON, release included. For cheap invariants
//     whose violation would corrupt survey data (index ranges, handle validity, mesh
//     wiring). Violation prints and aborts: a corrupt TIN must never reach a caller.
//   * KERNEL_DEBUG_ASSERT(cond, msg) — compiled in only when INGENEER_KERNEL_DEBUG_AUDIT
//     is defined. CMake defines that macro for Debug configurations (the dev, asan-ubsan
//     and tsan presets); it is absent in hardened/release builds. For expensive audits
//     (full constrained-Delaunay property, closure checks). The condition is NOT
//     evaluated when disabled.
#ifndef INGENEER_SURFACE_KERNEL_ASSERT_H
#define INGENEER_SURFACE_KERNEL_ASSERT_H

#include <cstdio>
#include <cstdlib>

#define KERNEL_ASSERT(cond, msg)                                                               \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "KERNEL_ASSERT failed: %s\n  %s\n  at %s:%d\n", #cond, (msg), \
                         __FILE__, __LINE__);                                                  \
            std::abort();                                                                      \
        }                                                                                      \
    } while (0)

#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
#define KERNEL_DEBUG_ASSERT(cond, msg) KERNEL_ASSERT(cond, msg)
#else
#define KERNEL_DEBUG_ASSERT(cond, msg) ((void)0)
#endif

#endif  // INGENEER_SURFACE_KERNEL_ASSERT_H
