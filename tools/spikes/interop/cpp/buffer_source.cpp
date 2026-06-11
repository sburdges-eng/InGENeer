// Phase 4 interop spike — C++ side of the zero-copy arena. Rich C++ internally, POD across
// the C ABI. No exceptions escape extern "C" (C-4.5): allocation failure returns NULL.
#include "buffer_source.h"

#include <new>
#include <vector>

struct ing_arena {
    std::vector<float> data;
    uint64_t generation = 1;
};

extern "C" {

ing_arena* ing_arena_create(size_t vertex_count) {
    auto* a = new (std::nothrow) ing_arena;
    if (!a) return nullptr;
    a->data.assign(vertex_count * 3, 0.0f);
    return a;
}

void ing_arena_destroy(ing_arena* a) { delete a; }

ing_view ing_arena_view(const ing_arena* a) {
    ing_view v;
    v.data = a->data.data();
    v.count = a->data.size();
    v.generation = a->generation;
    return v;
}

uint64_t ing_arena_step(ing_arena* a, uint32_t tick) {
    // Deterministic in-place mutation (a stand-in for an engine frame update).
    const float t = static_cast<float>(tick);
    for (size_t i = 0; i < a->data.size(); ++i) {
        a->data[i] = a->data[i] + 1.0f + t * 0.0f;  // bump; t kept for determinism contract
    }
    return ++a->generation;
}

int ing_view_is_current(const ing_arena* a, ing_view v) {
    return (a->generation == v.generation && v.data == a->data.data()) ? 1 : 0;
}

uint64_t ing_arena_generation(const ing_arena* a) { return a->generation; }

}  // extern "C"
