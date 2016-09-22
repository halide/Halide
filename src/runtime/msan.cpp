#include "HalideRuntime.h"
#include "device_buffer_utils.h"

extern "C" {

// This function is expected to be provided by LLVM as part of the MSAN implementation.
extern void AnnotateMemoryIsInitialized(const char *file, int line,
                                        const void *mem, size_t size);

WEAK void halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) {
    AnnotateMemoryIsInitialized("Halide", 0, ptr, (size_t) len);
}

// Default implementation marks the data pointed to by the buffer_t as initialized
// (but *not* the buffer_t itself); it takes pains to only mark the active memory ranges
// (skipping padding), and sorting into ranges to always mark the smallest number of
// ranges, in monotonically increasing memory order.
WEAK void halide_msan_annotate_buffer_is_initialized(void *user_context, buffer_t *b) {
    if (b == NULL) {
        return;
    }

    Halide::Runtime::Internal::device_copy c = Halide::Runtime::Internal::make_host_to_device_copy(b);
    if (c.chunk_size == 0) {
        return;
    }

    if (b->dev_dirty) {
        // buffer has been computed on a gpu, but not copied back:
        // don't annotate as initialized. (We'll assume that subsequent
        // calls to halide_copy_to_host will force another call.)
        return;
    }

    // TODO: Is this 32-bit or 64-bit? Leaving signed for now
    // in case negative strides.
    for (int w = 0; w < (int)c.extent[3]; w++) {
        for (int z = 0; z < (int)c.extent[2]; z++) {
            for (int y = 0; y < (int)c.extent[1]; y++) {
                for (int x = 0; x < (int)c.extent[0]; x++) {
                    const uint64_t off = (x * c.stride_bytes[0] +
                                    y * c.stride_bytes[1] +
                                    z * c.stride_bytes[2] +
                                    w * c.stride_bytes[3]);
                    const void *from = (void *)(c.src + off);
                    halide_msan_annotate_memory_is_initialized(user_context, from, c.chunk_size);
                }
            }
        }
    }
}

WEAK void halide_msan_annotate_buffer_is_initialized_as_destructor(void *user_context, void *b) {
    return halide_msan_annotate_buffer_is_initialized(user_context, (buffer_t *)b);
}

}
