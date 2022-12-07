#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

// An implementation of aligned_alloc() that is layered on top of malloc()/free().
WEAK_INLINE void *halide_internal_aligned_alloc(size_t alignment, size_t size) {
    // Alignment must be a power of two and >= sizeof(void*)
    halide_debug_assert(nullptr, is_power_of_two(alignment) && alignment >= sizeof(void *));

    // Allocate enough space for aligning the pointer we return.
    //
    // Always round allocations up to alignment size,
    // so that all allocators follow the behavior of aligned_alloc() and
    // return aligned pointer *and* aligned length.
    const size_t aligned_size = align_up(size + alignment, alignment);

    void *orig = ::malloc(aligned_size);
    if (orig == nullptr) {
        // Will result in a failed assertion and a call to halide_error
        return nullptr;
    }

    // malloc() and friends must return a pointer aligned to at least
    // alignof(std::max_align_t); we can't reasonably check that in
    // the runtime, but we can realistically assume it's at least
    // 8-aligned.
    halide_debug_assert(nullptr, (((uintptr_t)orig) % 8) == 0);

    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)align_up((uintptr_t)orig + sizeof(void *), alignment);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

WEAK_INLINE void halide_internal_aligned_free(void *ptr) {
    ::free(((void **)ptr)[-1]);
}

}  // extern "C"
