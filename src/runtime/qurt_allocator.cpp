#include "HalideRuntime.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

WEAK void *halide_default_malloc(void *user_context, size_t x) {
    // Hexagon needs up to 128 byte alignment.
    const size_t alignment = 128;

    // We also need to align the size of the buffer.
    x = (x + alignment - 1) & ~(alignment - 1);

    // Allocate enough space for aligning the pointer we return.
    void *orig = malloc(x + alignment);
    if (orig == NULL) {
        // Will result in a failed assertion and a call to halide_error
        return NULL;
    }
    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void*) - 1) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

WEAK void halide_default_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
}

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_malloc_t custom_malloc = halide_default_malloc;
WEAK halide_free_t custom_free = halide_default_free;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_malloc_t halide_set_custom_malloc(halide_malloc_t user_malloc) {
    halide_malloc_t result = custom_malloc;
    custom_malloc = user_malloc;
    return result;
}

WEAK halide_free_t halide_set_custom_free(halide_free_t user_free) {
    halide_free_t result = custom_free;
    custom_free = user_free;
    return result;
}

WEAK void *halide_malloc(void *user_context, size_t x) {
    return custom_malloc(user_context, x);
}

WEAK void halide_free(void *user_context, void *ptr) {
    custom_free(user_context, ptr);
}

}
