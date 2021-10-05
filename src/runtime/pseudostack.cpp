#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

#define HEAP_THRESHOLD (16 * 1024)

WEAK_INLINE __attribute__((used)) void *pseudostack_alloc(void *user_context, halide_pseudostack_slot_t *slot, size_t sz) {
    if (__builtin_expect(sz > slot->size, 0)) {
        if (slot->ptr && slot->size > HEAP_THRESHOLD) {
            halide_free(user_context, slot->ptr);
        }
        if (sz > HEAP_THRESHOLD) {
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
    if (slot->ptr && slot->size > HEAP_THRESHOLD) {
        halide_free(user_context, slot->ptr);
    }
    slot->size = 0;
    slot->ptr = nullptr;
}
}
