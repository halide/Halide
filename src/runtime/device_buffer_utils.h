#ifndef HALIDE_RUNTIME_DEVICE_BUFFER_UTILS_H
#define HALIDE_RUNTIME_DEVICE_BUFFER_UTILS_H

#include "HalideRuntime.h"
#include "device_interface.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal {


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
    uint64_t src, dst;
    // The multidimensional array of contiguous copy tasks that need to be done.
    uint64_t extent[MAX_COPY_DIMS];
    // The strides (in bytes) that separate adjacent copy tasks in each dimension.
    uint64_t stride_bytes[MAX_COPY_DIMS];
    // How many contiguous bytes to copy per task
    uint64_t chunk_size;
};


WEAK void copy_memory_helper(const device_copy &copy, int d, int64_t off) {
    // Skip size-1 dimensions
    while (d >= 0 && copy.extent[d] == 1) d--;
    
    if (d == -1) {
        const void *from = (void *)(copy.src + off);
        void *to = (void *)(copy.dst + off);
        memcpy(to, from, copy.chunk_size);
    } else {
        for (uint64_t i = 0; i < copy.extent[d]; i++) {
            copy_memory_helper(copy, d - 1, off);
            off += copy.stride_bytes[d];
        }
    }
}

WEAK void copy_memory(const device_copy &copy, void *user_context) {
    // If this is a zero copy buffer, these pointers will be the same.
    if (copy.src != copy.dst) {
        copy_memory_helper(copy, MAX_COPY_DIMS-1, 0);
    } else {
        debug(user_context) << "copy_memory: no copy needed as pointers are the same.\n";
    }
}

WEAK device_copy make_host_to_device_copy(const halide_buffer_t *buf) {
    // Make a copy job representing copying the first pixel only.
    device_copy c;
    c.src = (uint64_t)buf->host;
    c.dst = buf->device;
    c.chunk_size = buf->type.bytes();
    for (int i = 0; i < MAX_COPY_DIMS; i++) {
        c.extent[i] = 1;
        c.stride_bytes[i] = 0;
    }

    if (buf->dimensions > MAX_COPY_DIMS) {
        // This condition should also be checked for outside this fn.
        device_copy zero = {0};
        return zero;
    }

    if (buf->type.bits == 0) {
        // This buffer apparently represents no memory. Return a zero'd copy
        // task.
        device_copy zero = {0};
        return zero;
    }

    // Now expand it to copy all the pixels (one at a time) by taking
    // the extents and strides from the halide_buffer_t. Dimensions
    // are added to the copy by inserting it such that the stride is
    // in ascending order.
    for (int i = 0; i < buf->dimensions; i++) {
        // TODO: deal with negative strides.
        uint64_t stride_bytes = buf->dim[i].stride * buf->type.bytes();
        // Insert the dimension sorted into the buffer copy.
        int insert;
        for (insert = 0; insert < i; insert++) {
            // If the stride is 0, we put it at the end because it can't be
            // folded.
            if (stride_bytes < c.stride_bytes[insert] && stride_bytes != 0) {
                break;
            }
        }
        for (int j = i; j > insert; j--) {
            c.extent[j] = c.extent[j - 1];
            c.stride_bytes[j] = c.stride_bytes[j - 1];
        }
        // If the stride is 0, only copy it once.
        c.extent[insert] = stride_bytes != 0 ? buf->dim[i].extent : 1;
        debug(NULL) << "c.extent[" << insert << "] = " << (int)(c.extent[insert]) << "\n";
        c.stride_bytes[insert] = stride_bytes;
    };

    // Attempt to fold contiguous dimensions into the chunk size. Since the
    // dimensions are sorted by stride, and the strides must be greater than
    // or equal to the chunk size, this means we can just delete the innermost
    // dimension as long as its stride is equal to the chunk size.
    while(c.chunk_size == c.stride_bytes[0]) {
        // Fold the innermost dimension's extent into the chunk_size.
        c.chunk_size *= c.extent[0];

        // Erase the innermost dimension from the list of dimensions to
        // iterate over.
        for (int j = 1; j < MAX_COPY_DIMS; j++) {
            c.extent[j-1] = c.extent[j];
            c.stride_bytes[j-1] = c.stride_bytes[j];
        }
        c.extent[MAX_COPY_DIMS-1] = 1;
        c.stride_bytes[MAX_COPY_DIMS-1] = 0;
    }
    return c;
}

WEAK device_copy make_device_to_host_copy(const halide_buffer_t *buf) {
    // Just make a host to dev copy and swap src and dst
    device_copy c = make_host_to_device_copy(buf);
    uint64_t tmp = c.src;
    c.src = c.dst;
    c.dst = tmp;
    return c;
}

}}} // namespace Halide::Runtime::Internal

#endif // HALIDE_DEVICE_BUFFER_UTILS_H
