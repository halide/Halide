#include <stdlib.h>
#include <stdio.h>
#include "filter_headers.h"

struct filter {
    const char *name;
    int (*fn)(buffer_t *, // float32
              buffer_t *, // float64
              buffer_t *, // int8
              buffer_t *, // uint8
              buffer_t *, // int16
              buffer_t *, // uint16
              buffer_t *, // int32
              buffer_t *, // uint32
              buffer_t *, // int64
              buffer_t *, // uint64
              buffer_t *); // output
};

template<typename T>
T rand_value() {
    return (T)(rand() * 0.125) - 100;
}

// Even on android, we want errors to stdout
extern "C" void halide_print(void *, const char *msg) {
    printf("%s\n", msg);
}

template<typename T>
buffer_t make_buffer(int w, int h) {
    T *mem = (T *)memalign(128, w*h*sizeof(T));
    buffer_t buf = {0};
    buf.host = (uint8_t *)mem;
    buf.extent[0] = w;
    buf.extent[1] = h;
    buf.elem_size = sizeof(T);
    buf.stride[0] = 1;
    buf.stride[1] = w;

    for (int i = 0; i < w*h; i++) {
        mem[i] = rand_value<T>();
    }

    return buf;
}

#include "filters.h"

int main(int argc, char **argv) {
    const int W = 1024, H = 128;
    bool error = false;
    // Make some input buffers
    buffer_t bufs[] = {
        make_buffer<float>(W, H),
        make_buffer<double>(W, H),
        make_buffer<int8_t>(W, H),
        make_buffer<uint8_t>(W, H),
        make_buffer<int16_t>(W, H),
        make_buffer<uint16_t>(W, H),
        make_buffer<int32_t>(W, H),
        make_buffer<uint32_t>(W, H),
        make_buffer<int64_t>(W, H),
        make_buffer<uint64_t>(W, H)
    };

    buffer_t out = make_buffer<double>(1, 1);

    double *out_value = (double *)(out.host);

    for (int i = 0; filters[i].fn; i++) {
        filter f = filters[i];
        printf("Testing %s\n", f.name);
        f.fn(bufs + 0,
             bufs + 1,
             bufs + 2,
             bufs + 3,
             bufs + 4,
             bufs + 5,
             bufs + 6,
             bufs + 7,
             bufs + 8,
             bufs + 9,
             &out);
        if (*out_value) {
            printf("Error: %f\n", *out_value);
            error = true;
        }
    }

    for (int i = 0; i < sizeof(bufs)/sizeof(buffer_t); i++) {
        free(bufs[i].host);
    }
    free(out.host);

    if (!error) {
        printf ("Success!\n");
        return 0;
    } else {
        printf ("Error occurred\n");
        return -1;
    }
}
