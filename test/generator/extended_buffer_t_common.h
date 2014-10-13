#ifndef EXTENDED_BUFFER_T_COMMON_H
#define EXTENDED_BUFFER_T_COMMON_H

#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED
#include <stdint.h>
typedef struct buffer_t {
    uint64_t dev;
    uint8_t* host;
    int32_t extent[4];
    int32_t stride[4];
    int32_t min[4];
    int32_t elem_size;
    bool host_dirty;
    bool dev_dirty;
} buffer_t;
#endif


// A struct that extends buffer_t with an extra field
struct fancy_buffer_t : public buffer_t {
    int extra_field;

    // Build a fancy buffer_t from a buffer_t pointer.
    fancy_buffer_t(const buffer_t *buf) : buffer_t(*buf), extra_field(0) {}
};

extern "C" int fancy_buffer_t_get_extra_field(const fancy_buffer_t *buf) {
    return buf->extra_field;
}

#endif
