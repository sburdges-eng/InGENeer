// Phase 8 prerequisite spike — CPU-only AddressSanitizer exercise of the H-27 arena
// lifetime logic (no Metal: GPU + ASan interactions are unreliable, so the quarantine /
// realloc-under-read contract is sanitized here and the GPU ordering is proven separately
// by swift/metal_main.swift).
//
// Simulates the render loop: take a handle to the live page, realloc the arena, then READ
// the old page (exactly what an in-flight command buffer does). If quarantine were broken
// (use-after-free), ASan aborts here.
#include "metal_arena.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    constexpr size_t kBaseFloats = 12288;
    constexpr uint32_t kIters = 1000;

    ing_marena* a = ing_marena_create(kBaseFloats);
    assert(a != nullptr);
    assert(reinterpret_cast<uintptr_t>(ing_marena_data(a)) % 16384 == 0);
    assert(ing_marena_bytes(a) % 16384 == 0);

    for (uint32_t i = 0; i < kIters; ++i) {
        ing_marena_fill(a, i);
        float* old_ptr = ing_marena_data(a);
        const size_t old_n = ing_marena_float_count(a);
        const uint64_t gen = ing_marena_generation(a);

        double expected = 0.0;
        for (size_t k = 0; k < old_n; ++k) expected += static_cast<double>(old_ptr[k]);

        // Realloc while the "command buffer" still holds the old page.
        const uint64_t new_gen = ing_marena_grow(a, kBaseFloats + ((i % 7) + 1) * 96);
        assert(new_gen == gen + 1);
        assert(ing_marena_handle_is_current(a, gen) == 0);   // stale handle invalidated
        assert(ing_marena_handle_is_current(a, new_gen) == 1);
        assert(ing_marena_pages_quarantined(a) == 1);        // old page alive, quarantined

        // "GPU" read of the OLD page after the realloc — ASan flags this if freed early.
        double got = 0.0;
        for (size_t k = 0; k < old_n; ++k) got += static_cast<double>(old_ptr[k]);
        assert(got == expected);

        // Completion handler -> bytesNoCopy deallocator notify; arena frees the page now.
        const int rc = ing_marena_page_release(a, old_ptr);
        assert(rc == 1);
        assert(ing_marena_pages_quarantined(a) == 0);
        assert(ing_marena_pages_released(a) == static_cast<uint64_t>(i) + 1);
    }

    // Live-page notify path: deallocator fires while the page is still live; the next grow
    // frees it immediately instead of quarantining.
    const int rc = ing_marena_page_release(a, ing_marena_data(a));
    assert(rc == 2);
    assert(ing_marena_grow(a, kBaseFloats) != 0);
    assert(ing_marena_pages_quarantined(a) == 0);

    // Destroy with the live page (and, in general, any quarantined pages) still owned —
    // the arena is the single owner and frees everything (no leaks under ASan).
    ing_marena_destroy(a);

    std::printf("PASS asan: %u realloc-with-quarantined-read iterations clean (UAF-free)\n",
                kIters);
    return 0;
}
