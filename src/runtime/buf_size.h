#ifndef HALIDE_BUF_SIZE_H
#define HALIDE_BUF_SIZE_H

// TODO: in new buffer_t, add an inline method to do this and kill this file.

// Compute the total amount of memory we'd need to allocate on gpu to
// represent a given buffer (using the same strides as the host
// allocation).
WEAK size_t buf_size(const buffer_t *buf) {
    size_t size = buf->elem_size;
    for (size_t i = 0; i < sizeof(buf->stride) / sizeof(buf->stride[0]); i++) {
        size_t positive_stride;
        if (buf->stride[i] < 0) {
            positive_stride = (size_t)-buf->stride[i];
        } else {
            positive_stride = (size_t)buf->stride[i];
        }
        size_t total_dim_size = buf->elem_size * buf->extent[i] * positive_stride;
        if (total_dim_size > size) {
            size = total_dim_size;
        }
    }
    return size;
}

#endif // HALIDE_BUF_SIZE_H
