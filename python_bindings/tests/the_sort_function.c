/*
 * Compile using
 * gcc -std=c99 the_sort_function.c -shared -o the_sort_function.so
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 *   The raw representation of an image passed around by generated
 *   Halide code. It includes some stuff to track whether the image is
 *   not actually in main memory, but instead on a device (like a
 *   GPU). */
struct buffer_t {
    /** A device-handle for e.g. GPU memory used to back this buffer. */
    uint64_t dev;

    /** A pointer to the start of the data in main memory. */
    uint8_t* host;

    /** The size of the buffer in each dimension. */
    int32_t extent[4];

    /** Gives the spacing in memory between adjacent elements in the
     * given dimension.  The correct memory address for a load from
     * this buffer at position x, y, z, w is:
     * host + (x * stride[0] + y * stride[1] + z * stride[2] + w * stride[3]) *
     * elem_size By manipulating the strides and extents you can lazily crop,
     * transpose, and even flip buffers without modifying the data.
     */
    int32_t stride[4];

    /** Buffers often represent evaluation of a Func over some
     * domain. The min field encodes the top left corner of the
     * domain. */
    int32_t min[4];

    /** How many bytes does each buffer element take. This may be
     * replaced with a more general type code in the future. */
    int32_t elem_size;

    /** This should be true if there is an existing device allocation
     * mirroring this buffer, and the data has been modified on the
     * host side. */
    bool host_dirty;

    /** This should be true if there is an existing device allocation
                    mirroring this buffer, and the data has been modified on the
                    device side. */
    bool dev_dirty;

    // Some compilers will add extra padding at the end to ensure
    //     // the size is a multiple of 8; we'll do that explicitly so that
    //     // there is no ambiguity.
    //     HALIDE_ATTRIBUTE_ALIGN(1) uint8_t _padding[10 - sizeof(void *)];
};

/* Returns -1 if something went wrong, 0 otherwise */
int32_t the_sort_func(struct buffer_t* data) {
    // if(data.host == NULL)
    if (data->host == 0) {
        return -1;
    }

    if (data->extent[0] <= 0) {
        return -1;
    }

    for (size_t i = 1; i < 4; i += 1) {
        if (data->extent[i] != 0) {
            return -1;
        }
    }

    data->host[0] *= 5;

    return 0;
}
