#include "HalideRuntime.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void *aligned_malloc(size_t alignment, size_t size) {
    // We also need to align the size of the buffer.
    size = (size + alignment - 1) & ~(alignment - 1);

    // Allocate enough space for aligning the pointer we return.
    void *orig = malloc(size + alignment);
    if (orig == NULL) {
        // Will result in a failed assertion and a call to halide_error
        return NULL;
    }
    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void *) - 1) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

WEAK void aligned_free(void *ptr) {
    if (ptr) {
        free(((void **)ptr)[-1]);
    }
}

// We keep a small pool of small pre-allocated buffers for use by Halide
// code; some kernels end up doing per-scanline allocations and frees,
// which can cause a noticable performance impact on some workloads.
// 'num_buffers' is the number of pre-allocated buffers and 'buffer_size' is
// the size of each buffer. The pre-allocated buffers are shared among threads
// and we use __atomic_test_and_set primitive to synchronize the buffer
// allocation.
// TODO(psuriana): make num_buffers configurable by user
static const int num_buffers = 10;
static const int buffer_size = 1024 * 64;

// __atomic_test_and_set() return char-sized elements.
WEAK char buf_is_used[num_buffers];
WEAK void *mem_buf[num_buffers] = {
    NULL,
};

WEAK __attribute__((destructor)) void halide_allocator_cleanup() {
    for (int i = 0; i < num_buffers; ++i) {
        aligned_free(mem_buf[i]);
    }
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

WEAK void *halide_default_malloc(void *user_context, size_t x) {
    // Hexagon needs up to 128 byte alignment.
    const size_t alignment = 128;

    if (x <= buffer_size) {
        for (int i = 0; i < num_buffers; ++i) {
            // return value is true iff the previous contents were true,
            // so return value of false means we are claiming an unused slot
            if (__atomic_test_and_set(&buf_is_used[i], __ATOMIC_ACQUIRE) == false) {
                if (mem_buf[i] == NULL) {
                    mem_buf[i] = aligned_malloc(alignment, buffer_size);
                }
                return mem_buf[i];
            }
        }
    }

    return aligned_malloc(alignment, x);
}

WEAK void halide_default_free(void *user_context, void *ptr) {
    for (int i = 0; i < num_buffers; ++i) {
        if (mem_buf[i] == ptr) {
            __atomic_clear(&buf_is_used[i], __ATOMIC_RELEASE);
            return;
        }
    }

    aligned_free(ptr);
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
    // See TODO below.
    halide_print(NULL, "custom allocators not supported on Hexagon.\n");
    halide_malloc_t result = custom_malloc;
    custom_malloc = user_malloc;
    return result;
}

WEAK halide_free_t halide_set_custom_free(halide_free_t user_free) {
    // See TODO below.
    halide_print(NULL, "custom allocators not supported on Hexagon.\n");
    halide_free_t result = custom_free;
    custom_free = user_free;
    return result;
}

// TODO: These should be calling custom_malloc/custom_free, but globals are not
// initialized correctly when using mmap_dlopen. We need to fix this, then we
// can enable the custom allocators.
WEAK void *halide_malloc(void *user_context, size_t x) {
    return halide_default_malloc(user_context, x);
}

WEAK void halide_free(void *user_context, void *ptr) {
    halide_default_free(user_context, ptr);
}
}
