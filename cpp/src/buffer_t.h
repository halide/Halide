#ifndef HALIDE_BUFFER_T
#define HALIDE_BUFFER_T

#include <stdint.h>

/** \file 
 * This file defines the internal runtime representation of an image: buffer_t
 */


/**
 * The raw representation of an image passed around by generated
 * Halide code. It includes some stuff to track whether the image is
 * not actually in main memory, but instead on a device (like a
 * GPU). */
typedef struct buffer_t {
    uint8_t* host;
    uint64_t dev;
    bool host_dirty;
    bool dev_dirty;
    int32_t extent[4];
    int32_t stride[4];
    int32_t min[4];
    int32_t elem_size;
} buffer_t;

#endif
