#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

namespace {
// Should match the threshold defined in CodeGen_Internal.cpp
constexpr size_t heap_threshold = 16 * 1024;
}  // namespace

WEAK_INLINE __attribute__((used)) void *pseudostack_alloc(void *user_context, halide_pseudostack_slot_t *slot, size_t sz) {
    if (__builtin_expect(sz > slot->size, 0)) {
        if (slot->ptr && slot->cumulative_size > heap_threshold) {
            halide_free(user_context, slot->ptr);
        }
        slot->cumulative_size += sz;
        if (slot->cumulative_size > heap_threshold) {
            slot->ptr = halide_malloc(user_context, sz);
        } else {
            slot->ptr = nullptr;
        }
        slot->size = sz;
    }
    return slot->ptr;
}

// Only called as a destructor at function exit
WEAK_INLINE __attribute__((used)) void pseudostack_free(void *user_context, void *ptr) {
    halide_pseudostack_slot_t *slot = (halide_pseudostack_slot_t *)ptr;
    if (slot->ptr && slot->cumulative_size > heap_threshold) {
        halide_free(user_context, slot->ptr);
    }
    slot->ptr = nullptr;
    slot->size = 0;
    slot->cumulative_size = 0;
}
}
