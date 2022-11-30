#include "HalideRuntime.h"
#include "runtime_internal.h"

#include "printer.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

WEAK void *halide_default_malloc(void *user_context, size_t x) {
    // Allocate enough space for aligning the pointer we return.
    const size_t alignment = halide_malloc_alignment();

    // All bets are off if alignment is smaller than a pointer.
    halide_debug_assert(user_context, alignment >= sizeof(void *));

    // Always round allocations up to alignment size,
    // so that all allocators follow the behavior of aligned_alloc() and
    // return aligned pointer *and* aligned length.
    const size_t aligned_size = align_up(user_size, alignment);

    // We can save a bit of space by special-casing allocations < alignment
    // in size, which we can always fit entirely into 2*alignment.
    const size_t requested_size = (aligned_size <= alignment) ?
                                  (alignment * 2) :
                                  (aligned_size + sizeof(void *) + alignment - 1);

    // malloc() and friends must return a pointer aligned to at least
    // alignof(std::max_align_t); we can't reasonably check that in
    // the runtime, but we can realistically assume it's at least
    // 8-aligned.
    void *orig = malloc(requested_size);
    if (orig == nullptr) {
        // Will result in a failed assertion and a call to halide_error
        return nullptr;
    }

    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)align_up((uintptr_t)orig + sizeof(void *), alignment);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

WEAK void halide_default_free(void *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK halide_malloc_t custom_malloc = halide_default_malloc;
WEAK halide_free_t custom_free = halide_default_free;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

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
