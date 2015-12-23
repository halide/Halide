#ifndef EXTENDED_BUFFER_T_COMMON_H
#define EXTENDED_BUFFER_T_COMMON_H

#include "HalideRuntime.h"

// A struct that extends buffer_t with an extra field. Note that this would interfere with buffers of dimensionality > 8
struct fancy_buffer_t : public halide_buffer_t {
    int extra_field;

    // Build a fancy buffer_t from a buffer_t pointer.
    fancy_buffer_t(const halide_buffer_t *buf) : halide_buffer_t(*buf), extra_field(0) {}
};

extern "C" int fancy_buffer_t_get_extra_field(const fancy_buffer_t *buf) {
    return buf->extra_field;
}

#endif
