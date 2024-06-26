#ifndef HALIDE_RUNTIME_DEVICE_BUFFER_UTILS_H
#define HALIDE_RUNTIME_DEVICE_BUFFER_UTILS_H

#include "HalideRuntime.h"
#include "device_interface.h"
#include "printer.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// A host <-> dev copy should be done with the fewest possible number
// of contiguous copies to minimize driver overhead. If our
// halide_buffer_t has strides larger than its extents (e.g. because
// it represents a sub-region of a larger halide_buffer_t) we can't
// safely copy it back and forth using a single contiguous copy,
// because we'd clobber in-between values that another thread might be
// using.  In the best case we can do a single contiguous copy, but in
// the worst case we need to individually copy over every pixel.
//
// This problem is made extra difficult by the fact that the ordering
// of the dimensions in a halide_buffer_t doesn't relate to memory layout at
// all, so the strides could be in any order.
//
// We solve it by representing a copy job we need to perform as a
// device_copy struct. It describes a multi-dimensional array of
// copies to perform. Initially it describes copying over a single
// pixel at a time. We then try to discover contiguous groups of
// copies that can be coalesced into a single larger copy.

// The struct that describes a host <-> dev copy to perform.
#define MAX_COPY_DIMS 16
struct device_copy {
    // opaque handles for source and device memory.
    uint64_t src, dst;
    // The offset in the source memory to start
    uint64_t src_begin;
    // The multidimensional array of contiguous copy tasks that need to be done.
    uint64_t extent[MAX_COPY_DIMS];
    // The strides (in bytes) that separate adjacent copy tasks in each dimension.
    uint64_t src_stride_bytes[MAX_COPY_DIMS];
    uint64_t dst_stride_bytes[MAX_COPY_DIMS];
    // How many contiguous bytes to copy per task
    uint64_t chunk_size;
};

WEAK void copy_memory_helper(const device_copy &copy, int d, int64_t src_off, int64_t dst_off) {
    if ((d < -1) || (d >= MAX_COPY_DIMS)) {
        return;  // TODO(marcos): we should probably flag an error somehow here
    }

    // Skip size-1 dimensions
    while (d >= 0 && copy.extent[d] == 1) {
        d--;
    }

    if (d == -1) {
        const void *from = (void *)(copy.src + src_off);
        void *to = (void *)(copy.dst + dst_off);
        memcpy(to, from, copy.chunk_size);
    } else {
        for (uint64_t i = 0; i < copy.extent[d]; i++) {
            copy_memory_helper(copy, d - 1, src_off, dst_off);
            src_off += copy.src_stride_bytes[d];
            dst_off += copy.dst_stride_bytes[d];
        }
    }
}

WEAK void copy_memory(const device_copy &copy, void *user_context) {
    // If this is a zero copy buffer, these pointers will be the same.
    if (copy.src != copy.dst) {
        copy_memory_helper(copy, MAX_COPY_DIMS - 1, copy.src_begin, 0);
    } else {
        debug(user_context) << "copy_memory: no copy needed as pointers are the same.\n";
    }
}

// Fills the entire dst buffer, which must be contained within src
WEAK device_copy make_buffer_copy(const halide_buffer_t *src, bool src_host,
                                  const halide_buffer_t *dst, bool dst_host) {
    // Make a copy job representing copying the first pixel only.
    device_copy c;
    c.src = src_host ? (uint64_t)src->host : src->device;
    c.dst = dst_host ? (uint64_t)dst->host : dst->device;
    c.chunk_size = src->type.bytes();
    for (int i = 0; i < MAX_COPY_DIMS; i++) {
        c.extent[i] = 1;
        c.src_stride_bytes[i] = 0;
        c.dst_stride_bytes[i] = 0;
    }

    // Offset the src base pointer to the right point in its buffer.
    c.src_begin = 0;
    for (int i = 0; i < src->dimensions; i++) {
        c.src_begin += (int64_t)src->dim[i].stride * (int64_t)(dst->dim[i].min - src->dim[i].min);
    }
    c.src_begin *= c.chunk_size;

    if (src->dimensions != dst->dimensions ||
        src->type.bytes() != dst->type.bytes() ||
        dst->dimensions > MAX_COPY_DIMS) {
        // These conditions should also be checked for outside this fn.
        device_copy zero = {0};
        return zero;
    }

    if (c.chunk_size == 0) {
        // This buffer apparently represents no memory. Return a zero'd copy
        // task.
        device_copy zero = {0};
        return zero;
    }

    // Now expand it to copy all the pixels (one at a time) by taking
    // the extents and strides from the halide_buffer_ts. Dimensions
    // are added to the copy by inserting it such that the stride is
    // in ascending order in the dst.
    for (int i = 0; i < dst->dimensions; i++) {
        // TODO: deal with negative strides.
        uint64_t dst_stride_bytes = (uint64_t)dst->dim[i].stride * dst->type.bytes();
        uint64_t src_stride_bytes = (uint64_t)src->dim[i].stride * src->type.bytes();
        // Insert the dimension sorted into the buffer copy.
        int insert;
        for (insert = 0; insert < i; insert++) {
            // If the stride is 0, we put it at the end because it can't be
            // folded.
            if (dst_stride_bytes < c.dst_stride_bytes[insert] && dst_stride_bytes != 0) {
                break;
            }
        }
        for (int j = i; j > insert; j--) {
            c.extent[j] = c.extent[j - 1];
            c.dst_stride_bytes[j] = c.dst_stride_bytes[j - 1];
            c.src_stride_bytes[j] = c.src_stride_bytes[j - 1];
        }
        c.extent[insert] = dst->dim[i].extent;
        // debug(nullptr) << "c.extent[" << insert << "] = " << (int)(c.extent[insert]) << "\n";
        c.dst_stride_bytes[insert] = dst_stride_bytes;
        c.src_stride_bytes[insert] = src_stride_bytes;
    };

    // Attempt to fold contiguous dimensions into the chunk
    // size. Since the dimensions are sorted by stride, and the
    // strides must be greater than or equal to the chunk size, this
    // means we can just delete the innermost dimension as long as its
    // stride in both src and dst is equal to the chunk size.
    while (c.chunk_size &&
           c.chunk_size == c.src_stride_bytes[0] &&
           c.chunk_size == c.dst_stride_bytes[0]) {
        // Fold the innermost dimension's extent into the chunk_size.
        c.chunk_size *= c.extent[0];

        // Erase the innermost dimension from the list of dimensions to
        // iterate over.
        for (int j = 1; j < MAX_COPY_DIMS; j++) {
            c.extent[j - 1] = c.extent[j];
            c.src_stride_bytes[j - 1] = c.src_stride_bytes[j];
            c.dst_stride_bytes[j - 1] = c.dst_stride_bytes[j];
        }
        c.extent[MAX_COPY_DIMS - 1] = 1;
        c.src_stride_bytes[MAX_COPY_DIMS - 1] = 0;
        c.dst_stride_bytes[MAX_COPY_DIMS - 1] = 0;
    }
    return c;
}

WEAK device_copy make_host_to_device_copy(const halide_buffer_t *buf) {
    return make_buffer_copy(buf, true, buf, false);
}

WEAK device_copy make_device_to_host_copy(const halide_buffer_t *buf) {
    return make_buffer_copy(buf, false, buf, true);
}

// Caller is expected to verify that src->dimensions == dst->dimensions
ALWAYS_INLINE int64_t calc_device_crop_byte_offset(const struct halide_buffer_t *src, struct halide_buffer_t *dst) {
    int64_t offset = 0;
    for (int i = 0; i < src->dimensions; i++) {
        offset += (int64_t)(dst->dim[i].min - src->dim[i].min) * (int64_t)src->dim[i].stride;
    }
    offset *= src->type.bytes();
    return offset;
}

// Caller is expected to verify that src->dimensions == dst->dimensions + 1,
// and that slice_dim and slice_pos are valid within src
ALWAYS_INLINE int64_t calc_device_slice_byte_offset(const struct halide_buffer_t *src, int slice_dim, int slice_pos) {
    int64_t offset = (int64_t)(slice_pos - src->dim[slice_dim].min) * (int64_t)src->dim[slice_dim].stride;
    offset *= src->type.bytes();
    return offset;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_DEVICE_BUFFER_UTILS_H
