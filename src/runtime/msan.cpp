#include "HalideRuntime.h"
#include "device_buffer_utils.h"

extern "C" {

// This function is expected to be provided by LLVM as part of the MSAN implementation.
extern void AnnotateMemoryIsInitialized(const char *file, int line,
                                        const void *mem, size_t size);

WEAK void halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) {
    AnnotateMemoryIsInitialized("Halide", 0, ptr, (size_t) len);
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void annotate_helper(void *uc, const device_copy &c, int d, int64_t off) {
    while (d >= 0 && c.extent[d] == 1) d--;

    if (d == -1) {
        const void *from = (void *)(c.src + off);
        halide_msan_annotate_memory_is_initialized(uc, from, c.chunk_size);
    } else {
        for (uint64_t i = 0; i < c.extent[d]; i++) {
            annotate_helper(uc, c, d - 1, off);
            off += c.src_stride_bytes[d];
        }
    }
};

}}}

// Default implementation marks the data pointed to by the buffer_t as initialized
// (but *not* the buffer_t itself); it takes pains to only mark the active memory ranges
// (skipping padding), and sorting into ranges to always mark the smallest number of
// ranges, in monotonically increasing memory order.
WEAK void halide_msan_annotate_buffer_is_initialized(void *user_context, halide_buffer_t *b) {
    if (b == NULL) {
        return;
    }

    Halide::Runtime::Internal::device_copy c = Halide::Runtime::Internal::make_host_to_device_copy(b);
    if (c.chunk_size == 0) {
        return;
    }

    if (b->device_dirty()) {
        // buffer has been computed on a gpu, but not copied back:
        // don't annotate as initialized. (We'll assume that subsequent
        // calls to halide_copy_to_host will force another call.)
        return;
    }

    annotate_helper(user_context, c, MAX_COPY_DIMS-1, 0);
}

WEAK void halide_msan_annotate_buffer_is_initialized_as_destructor(void *user_context, void *b) {
    return halide_msan_annotate_buffer_is_initialized(user_context, (halide_buffer_t *)b);
}

}
