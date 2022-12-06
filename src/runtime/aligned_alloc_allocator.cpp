#include "HalideRuntime.h"
#include "runtime_internal.h"

#include "printer.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Read into a global to avoid making a call to halide_malloc_alignment()
// in every halide_malloc() call (halide_malloc_alignment() is required to
// return the same value every time).
WEAK size_t _alignment = (size_t)halide_malloc_alignment();

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

// aligned_alloc() is part of C11, and thus part of C++17, at least in theory...
// Frustratingly, it still isn't available everywhere (e.g. on Android,
// even when compiling with C++17, you must specify a certain SDK level),
// so we can't use it unconditionally, which is why it's not used in the
// standard posix_allocator.

extern void *aligned_alloc(size_t alignment, size_t size);
extern void free(void *);

WEAK void *halide_default_malloc(void *user_context, size_t x) {
    const size_t alignment = Halide::Runtime::Internal::_alignment;
    // The size parameter for aligned_alloc() must be an integral multiple of alignment.
    const size_t aligned_size = align_up(x, alignment);
    return aligned_alloc(alignment, aligned_size);
}

WEAK void halide_default_free(void *user_context, void *ptr) {
    free(ptr);
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
