// Phase 4 interop spike — stable C ABI for a C++-owned vertex arena shared zero-copy with
// Swift/Metal. No C++ types, no exceptions across this boundary (CONSTRAINTS C-4.5).
//
// Buffer lifetime contract (plan H-27): the C++ arena outlives every consumer view; handles
// are generation-stamped so a realloc invalidates stale in-flight views. This header is the
// spike's articulation of the eventual interop_core seam, not production code.
#ifndef INGENEER_SPIKE_BUFFER_SOURCE_H
#define INGENEER_SPIKE_BUFFER_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the C++-owned arena.
typedef struct ing_arena ing_arena;

// A zero-copy view onto arena memory. `generation` is stamped at hand-out; if the arena
// reallocates, the generation advances and any view with an older generation is stale.
typedef struct {
    const float* data;   // contiguous float triples (x,y,z), arena-owned, do NOT free
    size_t       count;  // number of floats (3 * vertex_count)
    uint64_t     generation;
} ing_view;

// Create an arena holding `vertex_count` xyz vertices. Returns NULL on allocation failure.
ing_arena* ing_arena_create(size_t vertex_count);

// Destroy the arena. All outstanding views become invalid.
void ing_arena_destroy(ing_arena* a);

// Hand out a zero-copy view (no allocation, no copy).
ing_view ing_arena_view(const ing_arena* a);

// Mutate every vertex in place (simulates a per-frame engine update). Returns the new
// generation. Pure CPU-side; deterministic given `tick` (no wall-clock — C-4.6).
uint64_t ing_arena_step(ing_arena* a, uint32_t tick);

// True if a view is still current for this arena (generation check; H-27).
int ing_view_is_current(const ing_arena* a, ing_view v);

// Trivial accessor — used to measure pure FFI boundary-crossing cost (no per-call work).
uint64_t ing_arena_generation(const ing_arena* a);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // INGENEER_SPIKE_BUFFER_SOURCE_H
