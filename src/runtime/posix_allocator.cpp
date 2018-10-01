#include "HalideRuntime.h"
#include "runtime_internal.h"

#include "printer.h"

extern "C" {

extern void *posix_memalign(void **memptr, size_t alignment, size_t size);
extern void free(void *);

WEAK void *halide_default_malloc(void *user_context, size_t x) {
    const size_t alignment = halide_malloc_alignment();
#ifdef DEBUG_RUNTIME
    // posix_memalign requires that the alignment be at least sizeof(void*).
    // Halide should always handle this, but check in Debug mode, just in case.
    if (alignment < sizeof(void*)) {
        halide_error(user_context, "halide_default_malloc: alignment is too small\n");
    }
#endif
    void *ptr = NULL;
    if (posix_memalign(&ptr, (size_t) alignment, x) != 0) {
        ptr = NULL;
    }
    return ptr;
}

WEAK void halide_default_free(void *user_context, void *ptr) {
    free(ptr);
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
