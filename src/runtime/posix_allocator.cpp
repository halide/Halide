#include "runtime_internal.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *(*halide_custom_malloc)(void *, size_t) = NULL;
WEAK void (*halide_custom_free)(void *, void *) = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                      void (*cust_free)(void *, void *)) {
    halide_custom_malloc = cust_malloc;
    halide_custom_free = cust_free;
}

WEAK void *halide_malloc(void *user_context, size_t x) {
    if (halide_custom_malloc) {
        return halide_custom_malloc(user_context, x);
    } else {
        void *orig = malloc(x+40);
        if (orig == NULL) {
            // Will result in a failed assertion and a call to halide_error
            return NULL;
        }
        // Round up to next multiple of 32. Should add at least 8 bytes so we can fit the original pointer.
        void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
        ((void **)ptr)[-1] = orig;
        return ptr;
    }
}

WEAK void halide_free(void *user_context, void *ptr) {
    if (halide_custom_free) {
        halide_custom_free(user_context, ptr);
    } else {
        free(((void**)ptr)[-1]);
    }
}

}
