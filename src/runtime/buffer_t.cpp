#include "HalideRuntime.h"

// Structs are annoying to deal with from within Halide Stmts. These
// utility functions are for dealing with buffer_t in that
// context. They are not intended for use outside of Halide code, and
// not exposed in HalideRuntime.h. The symbols are private to the
// module and should be inlined and then stripped. Definitions here
// are repeated in CodeGen_C.cpp, so changes here should be reflected
// there too.

extern "C" {

__attribute__((always_inline, weak))
uint8_t *_halide_buffer_get_host(const buffer_t *buf) {
    return buf->host;
}

__attribute__((always_inline, weak))
uint64_t _halide_buffer_get_dev(const buffer_t *buf) {
    return buf->dev;
}

__attribute__((always_inline, weak))
int _halide_buffer_get_min(const buffer_t *buf, int d) {
    return buf->min[d];
}

__attribute__((always_inline, weak))
int _halide_buffer_get_max(const buffer_t *buf, int d) {
    return buf->min[d] + buf->extent[d] - 1;
}

__attribute__((always_inline, weak))
bool _halide_buffer_get_host_dirty(const buffer_t *buf) {
    return buf->host_dirty;
}

__attribute__((always_inline, weak))
bool _halide_buffer_get_dev_dirty(const buffer_t *buf) {
    return buf->dev_dirty;
}

__attribute__((always_inline, weak))
int _halide_buffer_set_host_dirty(buffer_t *buf, bool val) {
    buf->host_dirty = val;
    return 0;
}

__attribute__((always_inline, weak))
int _halide_buffer_set_dev_dirty(buffer_t *buf, bool val) {
    buf->dev_dirty = val;
    return 0;
}

__attribute__((always_inline, weak))
buffer_t *_halide_buffer_init(buffer_t *dst, uint8_t *host, uint64_t dev, int /*type_code*/, int type_bits, int dimensions,
                              const int *min, const int *extent, const int *stride, bool host_dirty, bool dev_dirty) {
    dst->host = host;
    dst->dev = dev;
    dst->elem_size = (type_bits + 7) / 8;
    dst->host_dirty = host_dirty;
    dst->dev_dirty = dev_dirty;
    for (int i = 0; i < dimensions; i++) {
        dst->min[i] = min[i];
        dst->extent[i] = extent[i];
        dst->stride[i] = stride[i];
    }
    for (int i = dimensions; i < 4; i++) {
        dst->min[i] = 0;
        dst->extent[i] = 0;
        dst->stride[i] = 0;
    }
    return dst;
}

}
