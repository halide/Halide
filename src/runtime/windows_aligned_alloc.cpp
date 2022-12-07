#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

#ifdef DEBUG_RUNTIME
extern void *_aligned_malloc_dbg(size_t size, size_t alignment, const char *filename, int linenumber);
extern void _aligned_free_dbg(void *);
#else
extern void *_aligned_malloc(size_t size, size_t alignment);
extern void _aligned_free(void *);
#endif

// An implementation of aligned_alloc() that is layered on top of MSVC's _aligned_malloc/_aligned_free().
WEAK_INLINE void *halide_internal_aligned_alloc(size_t alignment, size_t size) {
    // Alignment must be a power of two and >= sizeof(void*)
    halide_debug_assert(nullptr, is_power_of_two(alignment) && alignment >= sizeof(void *));

    const size_t aligned_size = align_up(size, alignment);

    // Note: argument order is reversed from C11's aligned_alloc()
#ifdef DEBUG_RUNTIME
    return ::_aligned_malloc_dbg(aligned_size, alignment, __FILE__, __LINE__);
#else
    return ::_aligned_malloc(aligned_size, alignment);
#endif
}

WEAK_INLINE void halide_internal_aligned_free(void *ptr) {
#ifdef DEBUG_RUNTIME
    ::_aligned_free_dbg(ptr);
#else
    ::_aligned_free(ptr);
#endif
}

}  // extern "C"
