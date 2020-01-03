#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

inline WEAK __attribute__((always_inline)) __attribute__((used)) void *pseudostack_alloc(void *user_context, halide_pseudostack_slot_t *slot, size_t sz) {
    if (__builtin_expect(sz > slot->size, 0)) {
        if (slot->ptr) halide_free(user_context, slot->ptr);
        slot->ptr = halide_malloc(user_context, sz);
        slot->size = sz;
    }
    return slot->ptr;
}

// Only called as a destructor at function exit
inline WEAK __attribute__((always_inline)) __attribute__((used)) void pseudostack_free(void *user_context, void *ptr) {
    halide_pseudostack_slot_t *slot = (halide_pseudostack_slot_t *)ptr;
    slot->size = 0;
    if (slot->ptr) halide_free(user_context, slot->ptr);
    slot->ptr = NULL;
}
}
