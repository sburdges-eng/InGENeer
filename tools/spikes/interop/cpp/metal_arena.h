// Phase 8 prerequisite spike — stable C ABI for a PAGE-ALIGNED C++-owned vertex arena shared
// zero-copy with Metal via `makeBuffer(bytesNoCopy:)`. No C++ types, no exceptions across
// this boundary (CONSTRAINTS C-4.5).
//
// Buffer lifetime contract (plan H-27), implemented here and proven by swift/metal_main.swift:
//   - the C++ arena outlives every MTLBuffer referencing its pages;
//   - a grow/realloc advances the generation; stale generation-stamped handles are refused
//     at the encode API layer (`ing_marena_handle_is_current`);
//   - the OLD page is not freed on realloc — it is QUARANTINED so in-flight GPU work reading
//     it stays valid;
//   - the bytesNoCopy deallocator is a no-op (it never frees) that NOTIFIES the arena via
//     `ing_marena_page_release`; only then does the arena free a quarantined page.
//
// Spike articulation of the eventual interop_core/RHI seam — not production code.
#ifndef INGENEER_SPIKE_METAL_ARENA_H
#define INGENEER_SPIKE_METAL_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the C++-owned page-aligned arena.
typedef struct ing_marena ing_marena;

// Create an arena holding `float_count` floats in a 16KB-page-aligned block whose byte
// length is rounded up to a whole number of pages (a `bytesNoCopy` requirement).
// Returns NULL on allocation failure.
ing_marena* ing_marena_create(size_t float_count);

// Destroy the arena: frees the live block and any still-quarantined pages. Callers must
// ensure every MTLBuffer deallocator has fired first (arena outlives all buffers — H-27).
void ing_marena_destroy(ing_marena* a);

// Current live block (16KB-aligned). Do NOT free; arena-owned.
float* ing_marena_data(ing_marena* a);

// Byte length of the live block, always a multiple of the 16KB page size (pass directly to
// `makeBuffer(bytesNoCopy:length:...)`).
size_t ing_marena_bytes(const ing_marena* a);

// Number of valid floats in the live block (<= bytes/4).
size_t ing_marena_float_count(const ing_marena* a);

// Current generation stamp (starts at 1; bumped by every grow).
uint64_t ing_marena_generation(const ing_marena* a);

// Deterministic in-place fill (simulates an engine frame write). Values are small integers
// (< 769) so float checksums are exact. No wall-clock (C-4.6).
void ing_marena_fill(ing_marena* a, uint32_t tick);

// Realloc: allocate a NEW page-aligned block, copy min(old,new) floats, QUARANTINE the old
// block (kept alive for in-flight GPU work), bump and return the new generation.
// Returns 0 on allocation failure (arena unchanged).
uint64_t ing_marena_grow(ing_marena* a, size_t new_float_count);

// 1 if `generation` matches the arena's current generation, else 0. The Swift encode layer
// calls this before encoding and REFUSES stale handles (H-27 invalidation).
int ing_marena_handle_is_current(const ing_marena* a, uint64_t generation);

// The bytesNoCopy deallocator entry point. A no-op with respect to the caller's memory
// (never frees Metal-side); notifies the arena that the GPU/Metal is done with `ptr`.
// Returns 1 if `ptr` was a quarantined page (arena frees it now), 2 if it is the live page
// (marked released; freed immediately on the next grow or at destroy), 0 if unknown.
int ing_marena_page_release(ing_marena* a, void* ptr);

// Pages currently alive in quarantine (allocated, awaiting GPU release).
uint64_t ing_marena_pages_quarantined(const ing_marena* a);

// Total quarantined pages freed so far via deallocator notification.
uint64_t ing_marena_pages_released(const ing_marena* a);

// Total deallocator notifications received (quarantined + live).
uint64_t ing_marena_release_notifications(const ing_marena* a);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // INGENEER_SPIKE_METAL_ARENA_H
