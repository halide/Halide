#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

// MSVC doesn't provide memalign or posix_memalign,
// but does provide its own API.
extern void *_aligned_malloc(size_t, size_t);
extern void _aligned_free(void *);

WEAK void *halide_default_malloc(void *user_context, size_t x) {
    const size_t alignment = halide_malloc_alignment();
    // Arguments are reverse order from memalign()
    return _aligned_malloc(x, alignment);
}

WEAK void halide_default_free(void *user_context, void *ptr) {
    // MSVC doesn't allow you to use free() for this.
    _aligned_free(ptr);
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
