#include "HalideRuntime.h"

extern "C" {

namespace Halide { namespace Runtime { namespace Internal {

struct Dimension {
  ptrdiff_t stride_bytes;
  size_t extent;
};

WEAK void halide_msan_annotate_buffer_is_initialized_internal(
    void *user_context, const Dimension *dims, int dims_count, int level, uint8_t* host) {

    const ptrdiff_t stride_bytes = dims[level].stride_bytes;
    const size_t extent = dims[level].extent;
    if (level == dims_count - 1) {
        const size_t swath_bytes = stride_bytes * extent;
        halide_msan_annotate_memory_is_initialized(user_context, host, swath_bytes);
        return;
    }

    for (size_t i = 0; i < extent; ++i) {
        halide_msan_annotate_buffer_is_initialized_internal(
            user_context, dims, dims_count, level + 1, host + i * stride_bytes);
    }
}

}}} // namespace Halide::Runtime::Internal

WEAK void halide_msan_annotate_memory_is_initialized(void *user_context, void *ptr, size_t len) {
    // Default implementation does nothing; you must override this and supply (e.g.)
    //    ANNOTATE_MEMORY_IS_INITIALIZED(ptr, len);
    // to mark the memory as initialized.
}

// Default implementation marks the data pointed to by the buffer_t as initialized
// (but *not* the buffer_t itself); it takes pains to only mark the active memory ranges
// (skipping padding), and sorting into ranges to always mark the smallest number of
// ranges, in monotonically increasing memory order.
WEAK void halide_msan_annotate_buffer_is_initialized(void *user_context, void *v) {
    halide_print(user_context, "halide_msan_annotate_buffer_is_initialized\n");
    buffer_t *b = (buffer_t *)v;
    if (b == NULL) {
        return;
    }

    enum { kMaxDims = 4 };
    Dimension dims[kMaxDims + 1];
    int dim_count = 0;
    for (int i = 0; i < kMaxDims; ++i) {
        if (b->extent[i] == 0) {
          break;
        }
        dims[dim_count].stride_bytes = static_cast<ptrdiff_t>(b->stride[i]) * b->elem_size;
        dims[dim_count].extent = b->extent[i];
        dim_count++;
    }
    if (dim_count == 0) {
        return;
    }

    // Sort in decreasing order of stride_bytes.
    // Naive sort is fine for typical use here.
    for (int i = 0; i < dim_count; ++i) {
        for (int j = i; j > 0 && dims[j-1].stride_bytes < dims[j].stride_bytes;) {
            Dimension tmp = dims[j];
            dims[j] = dims[j-1];
            dims[j-1] = tmp;
            j--;
        }
    }

    // Add an extra dimension to addresses individual elements. It encodes the
    // length of an element as a stride value. This is is necessary because the
    // dimension with the smallest stride is not necessarily dense.
    dims[dim_count].stride_bytes = static_cast<ptrdiff_t>(b->elem_size);
    dims[dim_count].extent = 1;
    dim_count++;

    // Merge dense dimensions.
    for (int i = dim_count - 2; i >= 0; --i) {
        if (dims[i].stride_bytes == static_cast<ptrdiff_t>(dims[i + 1].stride_bytes * dims[i + 1].extent)) {
          dim_count--;
        } else {
          break;
        }
    }

    halide_msan_annotate_buffer_is_initialized_internal(user_context, dims, dim_count, 0, b->host);    
}

}
