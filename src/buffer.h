#ifndef _BUFFER_T
#define _BUFFER_T

#include <stdint.h>

typedef struct buffer_t {
    uint8_t* host;
    uint64_t dev; // hard code 64-bits for now - later opaque struct*? size_t?
    bool host_dirty;
    bool dev_dirty;
    size_t dims[4];
    size_t elem_size;
    // TODO: strides
} buffer_t;

#endif //_BUFFER_T
