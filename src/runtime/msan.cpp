#include "HalideRuntime.h"
#include "device_buffer_utils.h"

extern "C" {

// This function is expected to be provided by LLVM as part of the MSAN implementation.
extern void __msan_unpoison(const volatile void *a, size_t size);
extern void __msan_check_mem_is_initialized(const volatile void *x, size_t size);
extern long __msan_test_shadow(const volatile void *x, size_t size);

WEAK int halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) {
    __msan_unpoison(ptr, (size_t)len);
    return 0;
}

WEAK int halide_msan_check_memory_is_initialized(void *user_context, const void *ptr, uint64_t len, const char *name) {
    long offset = __msan_test_shadow(ptr, (size_t)len);
    if (offset >= 0) {
        print(user_context) << "MSAN failure detected for " << name << " @ " << ptr << " + " << (int)offset << "\n";
        // This is slightly redundant but gives better output.
        __msan_check_mem_is_initialized(ptr, (size_t)len);
    }
    return 0;
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void annotate_helper(void *uc, const device_copy &c, int d, int64_t off) {
    while (d >= 0 && c.extent[d] == 1) {
        d--;
    }

    if (d == -1) {
        const void *from = (void *)(c.src + off);
        (void)halide_msan_annotate_memory_is_initialized(uc, from, c.chunk_size);  // ignore errors
    } else {
        for (uint64_t i = 0; i < c.extent[d]; i++) {
            annotate_helper(uc, c, d - 1, off);
            off += c.src_stride_bytes[d];
        }
    }
};

WEAK void check_helper(void *uc, const device_copy &c, int d, int64_t off, const char *buf_name) {
    while (d >= 0 && c.extent[d] == 1) {
        d--;
    }

    if (d == -1) {
        const void *from = (void *)(c.src + off);
        (void)halide_msan_check_memory_is_initialized(uc, from, c.chunk_size, buf_name);  // ignore errors
    } else {
        for (uint64_t i = 0; i < c.extent[d]; i++) {
            check_helper(uc, c, d - 1, off, buf_name);
            off += c.src_stride_bytes[d];
        }
    }
};

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

// Default implementation marks the data pointed to by the halide_buffer_t as initialized
// (but *not* the halide_buffer_t itself); it takes pains to only mark the active memory ranges
// (skipping padding), and sorting into ranges to always mark the smallest number of
// ranges, in monotonically increasing memory order.
WEAK int halide_msan_annotate_buffer_is_initialized(void *user_context, halide_buffer_t *b) {
    if (b == nullptr) {
        return 0;
    }

    Halide::Runtime::Internal::device_copy c = Halide::Runtime::Internal::make_host_to_device_copy(b);
    if (c.chunk_size == 0) {
        return 0;
    }

    if (b->device_dirty()) {
        // buffer has been computed on a gpu, but not copied back:
        // don't annotate as initialized. (We'll assume that subsequent
        // calls to halide_copy_to_host will force another call.)
        return 0;
    }

    annotate_helper(user_context, c, MAX_COPY_DIMS - 1, 0);
    return 0;
}

WEAK void halide_msan_annotate_buffer_is_initialized_as_destructor(void *user_context, void *b) {
    (void)halide_msan_annotate_buffer_is_initialized(user_context, (halide_buffer_t *)b);
}

WEAK int halide_msan_check_buffer_is_initialized(void *user_context, halide_buffer_t *b, const char *buf_name) {
    if (b == nullptr) {
        return 0;
    }

    (void)halide_msan_check_memory_is_initialized(user_context, (void *)b, sizeof(*b), buf_name);                              // ignore errors
    (void)halide_msan_check_memory_is_initialized(user_context, (void *)b->dim, b->dimensions * sizeof(b->dim[0]), buf_name);  // ignore errors

    Halide::Runtime::Internal::device_copy c = Halide::Runtime::Internal::make_host_to_device_copy(b);
    if (c.chunk_size == 0) {
        return 0;
    }

    if (b->device_dirty()) {
        // buffer has been computed on a gpu, but not copied back:
        // don't check. (We'll assume that subsequent calls to
        // halide_copy_to_host will force another call.)
        return 0;
    }

    check_helper(user_context, c, MAX_COPY_DIMS - 1, 0, buf_name);
    return 0;
}

}  // extern "C"
