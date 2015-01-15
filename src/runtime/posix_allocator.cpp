#include "runtime_internal.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

}

struct allocator {
    void *(*cust_malloc)(void *, size_t);
    void (*cust_free)(void *, void *);
};

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *default_malloc(void *user_context, size_t x) {
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

WEAK void default_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
}

WEAK struct allocator custom_allocator =  { default_malloc, default_free };

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK struct allocator halide_set_custom_allocator(struct allocator allocator) {
    struct allocator result = custom_allocator;
    custom_allocator = allocator;
    return result;
}

WEAK void *halide_malloc(void *user_context, size_t x) {
    return custom_allocator.cust_malloc(user_context, x);
}

WEAK void halide_free(void *user_context, void *ptr) {
    custom_allocator.cust_free(user_context, ptr);
}

}
