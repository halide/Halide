#ifndef _BUFFER_T
#define _BUFFER_T

typedef struct buffer_t {
    char* host;
    unsigned long long dev; // hard code 64-bits for now - later opaque struct*?
    bool host_dirty;
    bool dev_dirty;
    size_t dims[4];
    size_t elem_size;
    // TODO: strides
} buffer_t;

#endif //_BUFFER_T
