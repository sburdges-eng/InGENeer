// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/kernel_assert.h — tiered kernel assertions for pointcloud_core.
// Local copy of libs/surface_core/include/ingeneer/surface/kernel_assert.h (scope
// discipline: promote to a shared header only when a third engine needs it). Tiers:
//   * KERNEL_ASSERT       — always on (release included); cheap data-corruption guards.
//   * KERNEL_DEBUG_ASSERT — compiled in only when INGENEER_KERNEL_DEBUG_AUDIT is defined
//     (CMake defines it for Debug: dev, asan-ubsan, tsan). Condition not evaluated when off.
#ifndef INGENEER_POINTCLOUD_KERNEL_ASSERT_H
#define INGENEER_POINTCLOUD_KERNEL_ASSERT_H

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

#endif  // INGENEER_POINTCLOUD_KERNEL_ASSERT_H
