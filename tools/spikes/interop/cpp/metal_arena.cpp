// Phase 8 prerequisite spike — C++ side of the page-aligned Metal-shareable arena.
// Rich C++ internally, POD across the C ABI; no exceptions escape extern "C" (C-4.5).
//
// H-27 lifetime contract: grow QUARANTINES the old page instead of freeing it; the
// bytesNoCopy deallocator notification (`ing_marena_page_release`) is the only thing that
// lets the arena free a quarantined page. The deallocator may fire on a Metal-internal
// thread, so all state is mutex-guarded.
#include "metal_arena.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>

namespace {

// macOS arm64 VM page size; `makeBuffer(bytesNoCopy:)` requires page-aligned pointer and a
// page-multiple length.
constexpr size_t kPageSize = 16384;

size_t round_to_pages(size_t bytes) {
    const size_t pages = (bytes + kPageSize - 1) / kPageSize;
    return (pages == 0 ? 1 : pages) * kPageSize;
}

float* alloc_pages(size_t bytes) {
    void* p = nullptr;
    if (posix_memalign(&p, kPageSize, bytes) != 0) return nullptr;
    std::memset(p, 0, bytes);
    return static_cast<float*>(p);
}

}  // namespace

struct ing_marena {
    mutable std::mutex mu;
    float* data = nullptr;
    size_t float_count = 0;
    size_t bytes = 0;
    uint64_t generation = 1;
    // GPU said it is done with the LIVE page (deallocator fired before any grow): the next
    // grow may free the old page immediately instead of quarantining it.
    bool live_release_noted = false;
    std::unordered_map<void*, size_t> quarantine;  // old page -> byte length, kept alive
    uint64_t released = 0;                          // quarantined pages freed after notify
    uint64_t notifications = 0;                     // all deallocator notifications
};

extern "C" {

ing_marena* ing_marena_create(size_t float_count) {
    auto* a = new (std::nothrow) ing_marena;
    if (!a) return nullptr;
    a->bytes = round_to_pages(float_count * sizeof(float));
    a->data = alloc_pages(a->bytes);
    if (!a->data) {
        delete a;
        return nullptr;
    }
    a->float_count = float_count;
    return a;
}

void ing_marena_destroy(ing_marena* a) {
    if (!a) return;
    {
        std::lock_guard<std::mutex> lk(a->mu);
        std::free(a->data);
        for (auto& kv : a->quarantine) std::free(kv.first);
        a->quarantine.clear();
    }
    delete a;
}

float* ing_marena_data(ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->data;
}

size_t ing_marena_bytes(const ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->bytes;
}

size_t ing_marena_float_count(const ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->float_count;
}

uint64_t ing_marena_generation(const ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->generation;
}

void ing_marena_fill(ing_marena* a, uint32_t tick) {
    std::lock_guard<std::mutex> lk(a->mu);
    const size_t t = static_cast<size_t>(tick);
    for (size_t i = 0; i < a->float_count; ++i) {
        // Small integers (< 769): float sums of these are exact, so CPU and GPU checksums
        // can be compared with == regardless of summation order.
        a->data[i] = static_cast<float>((i * 7 + t * 13) % 769);
    }
}

uint64_t ing_marena_grow(ing_marena* a, size_t new_float_count) {
    std::lock_guard<std::mutex> lk(a->mu);
    const size_t new_bytes = round_to_pages(new_float_count * sizeof(float));
    float* np = alloc_pages(new_bytes);
    if (!np) return 0;
    std::memcpy(np, a->data, std::min(a->float_count, new_float_count) * sizeof(float));
    if (a->live_release_noted) {
        // GPU already declared itself done with the old live page: free immediately.
        std::free(a->data);
    } else {
        // H-27: the old page may back in-flight GPU work — quarantine, never free here.
        a->quarantine.emplace(a->data, a->bytes);
    }
    a->live_release_noted = false;
    a->data = np;
    a->bytes = new_bytes;
    a->float_count = new_float_count;
    return ++a->generation;  // stale handles are now invalid
}

int ing_marena_handle_is_current(const ing_marena* a, uint64_t generation) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->generation == generation ? 1 : 0;
}

int ing_marena_page_release(ing_marena* a, void* ptr) {
    std::lock_guard<std::mutex> lk(a->mu);
    ++a->notifications;
    auto it = a->quarantine.find(ptr);
    if (it != a->quarantine.end()) {
        // The arena (owner) frees; the Metal deallocator itself freed nothing.
        std::free(it->first);
        a->quarantine.erase(it);
        ++a->released;
        return 1;
    }
    if (ptr == a->data) {
        a->live_release_noted = true;
        return 2;
    }
    return 0;
}

uint64_t ing_marena_pages_quarantined(const ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return static_cast<uint64_t>(a->quarantine.size());
}

uint64_t ing_marena_pages_released(const ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->released;
}

uint64_t ing_marena_release_notifications(const ing_marena* a) {
    std::lock_guard<std::mutex> lk(a->mu);
    return a->notifications;
}

}  // extern "C"
