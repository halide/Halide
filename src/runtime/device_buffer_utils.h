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
    // opaque handles (host or device) for source and destination memory.
    uint64_t src, dst;
    // The offset in the source and destination memory to start
    uint64_t src_begin, dst_begin;
    // The multidimensional array of contiguous copy tasks that need to be done.
    uint64_t extent[MAX_COPY_DIMS];
    // The strides (in bytes) that separate adjacent copy tasks in each dimension.
    uint64_t src_stride_bytes[MAX_COPY_DIMS];
    uint64_t dst_stride_bytes[MAX_COPY_DIMS];
    // How many contiguous bytes to copy per task
    uint64_t chunk_size;
    // The number of dimensions each task spans, counting its contiguous
    // chunk: 1 for just the chunk, or 2 or 3 for a 2D or 3D rectangle, for
    // runtimes that can copy one in a single operation. A task is
    // rect_extent[1] slices of rect_extent[0] rows of chunk_size bytes, with
    // rows {src,dst}_rect_pitch[0] bytes apart and slices
    // {src,dst}_rect_pitch[1] bytes apart. Each pitch is at least the size of
    // what it steps over, and the slice pitch is a multiple of the row pitch.
    // Dimensions a task doesn't span have extent 1.
    int rect_dims;
    uint64_t rect_extent[2];
    uint64_t src_rect_pitch[2], dst_rect_pitch[2];
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
        for (uint64_t z = 0; z < copy.rect_extent[1]; z++) {
            for (uint64_t y = 0; y < copy.rect_extent[0]; y++) {
                const void *from = (void *)(copy.src + src_off +
                                            z * copy.src_rect_pitch[1] + y * copy.src_rect_pitch[0]);
                void *to = (void *)(copy.dst + dst_off +
                                    z * copy.dst_rect_pitch[1] + y * copy.dst_rect_pitch[0]);
                memcpy(to, from, copy.chunk_size);
            }
        }
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
        copy_memory_helper(copy, MAX_COPY_DIMS - 1, copy.src_begin, copy.dst_begin);
    } else {
        debug(user_context) << "copy_memory: no copy needed as pointers are the same.\n";
    }
}

ALWAYS_INLINE void erase_innermost_dim(device_copy &c) {
    for (int j = 1; j < MAX_COPY_DIMS; j++) {
        c.extent[j - 1] = c.extent[j];
        c.src_stride_bytes[j - 1] = c.src_stride_bytes[j];
        c.dst_stride_bytes[j - 1] = c.dst_stride_bytes[j];
    }
    c.extent[MAX_COPY_DIMS - 1] = 1;
    c.src_stride_bytes[MAX_COPY_DIMS - 1] = 0;
    c.dst_stride_bytes[MAX_COPY_DIMS - 1] = 0;
}

// Whether stride (in bytes) can be the row pitch (k = 0) or slice pitch
// (k = 1) of the copy's tasks, given the pitches before it.
ALWAYS_INLINE bool valid_rect_pitch(const device_copy &c, int k, uint64_t stride,
                                    const uint64_t *pitch) {
    if ((int64_t)stride <= 0) {
        return false;
    } else if (k == 0) {
        return stride >= c.chunk_size;
    } else {
        return stride >= c.rect_extent[0] * pitch[0] && stride % pitch[0] == 0;
    }
}

// All crops are supported. It copies the maximum amount of pixels from src to dst.
// That maximum number of pixels is determined by the overlapping region of the two
// buffers. This means that you can use it in scenarios:
//  1) Fill the entire dst buffer, when the dst buffer bounds are contained within src.
//  2) Copy the entire src buffer, when the src buffer bounds are contained within dst, to dst.
//  3) Copy only the overlapping region between two buffers, from src to dst.
//
// A runtime that can copy a 2D (or 3D) rectangle in one operation passes
// max_rect_dims = 2 (or 3) to have each task span up to that many
// dimensions (see device_copy).
WEAK device_copy make_buffer_copy(const halide_buffer_t *src, bool src_host,
                                  const halide_buffer_t *dst, bool dst_host,
                                  int max_rect_dims = 1) {
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
    c.rect_dims = 1;
    for (int i = 0; i < 2; i++) {
        c.rect_extent[i] = 1;
        c.src_rect_pitch[i] = 0;
        c.dst_rect_pitch[i] = 0;
    }

    if (src->dimensions != dst->dimensions ||
        src->type.bytes() != dst->type.bytes() ||
        dst->dimensions > MAX_COPY_DIMS) {
        // These conditions should also be checked for outside this fn.
        // The check has to happen before the loops below, which index
        // dst->dim[i] up to src->dimensions.
        device_copy zero = {0};
        return zero;
    }

    // Offset the src and dst base pointer to the right point in their buffer.
    c.src_begin = 0;
    c.dst_begin = 0;
    for (int i = 0; i < src->dimensions; i++) {
        int64_t dim_diff = int64_t(dst->dim[i].min - src->dim[i].min);
        if (dim_diff > 0) {
            c.src_begin += (int64_t)src->dim[i].stride * dim_diff;
        } else {
            c.dst_begin += (int64_t)dst->dim[i].stride * (-dim_diff);
        }
    }
    c.src_begin *= c.chunk_size;
    c.dst_begin *= c.chunk_size;

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
        // Skip extent-one dimensions
        if (dst->dim[i].extent == 1) {
            continue;
        }
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
        c.extent[insert] = min(src->dim[i].extent, dst->dim[i].extent);
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
        erase_innermost_dim(c);
    }

    // Move the innermost remaining dimensions into the rect dimensions, as
    // long as their strides are valid pitches.
    while (c.rect_dims < max_rect_dims && c.rect_dims < 3 &&
           c.extent[0] > 1 &&
           valid_rect_pitch(c, c.rect_dims - 1, c.src_stride_bytes[0], c.src_rect_pitch) &&
           valid_rect_pitch(c, c.rect_dims - 1, c.dst_stride_bytes[0], c.dst_rect_pitch)) {
        c.rect_extent[c.rect_dims - 1] = c.extent[0];
        c.src_rect_pitch[c.rect_dims - 1] = c.src_stride_bytes[0];
        c.dst_rect_pitch[c.rect_dims - 1] = c.dst_stride_bytes[0];
        c.rect_dims++;
        erase_innermost_dim(c);
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
