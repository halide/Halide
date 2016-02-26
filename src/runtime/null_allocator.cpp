#include "runtime_internal.h"

#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

// For OSes that can't reference external symbols, we provide an
// allocator implementation that requires a custom allocator to be
// set.
WEAK halide_malloc_t custom_malloc = NULL;
WEAK halide_free_t custom_free = NULL;

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

}  // extern "C"
