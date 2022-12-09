#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

extern void *aligned_alloc(size_t alignment, size_t size);
extern void free(void *);

// An implementation of aligned_alloc() that is layered on top of aligned_alloc().
WEAK_INLINE void *halide_internal_aligned_alloc(size_t alignment, size_t size) {
    // Alignment must be a power of two and >= sizeof(void*)
    halide_debug_assert(nullptr, is_power_of_two(alignment) && alignment >= sizeof(void *));

    const size_t aligned_size = align_up(size, alignment);
    return ::aligned_alloc(alignment, aligned_size);
}

WEAK_INLINE void halide_internal_aligned_free(void *ptr) {
    ::free(ptr);
}

}  // extern "C"
