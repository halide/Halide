#include "filter_headers.h"
#include <HalideRuntime.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef __APPLE__
extern "C" void *memalign(size_t alignment, size_t size);
#endif

struct filter {
    const char *name;
    int (*fn)(halide_buffer_t *,   // float32
              halide_buffer_t *,   // float64
              halide_buffer_t *,   // float16
              halide_buffer_t *,   // bfloat16
              halide_buffer_t *,   // int8
              halide_buffer_t *,   // uint8
              halide_buffer_t *,   // int16
              halide_buffer_t *,   // uint16
              halide_buffer_t *,   // int32
              halide_buffer_t *,   // uint32
              halide_buffer_t *,   // int64
              halide_buffer_t *,   // uint64
              halide_buffer_t *);  // output
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
halide_buffer_t make_buffer(int w, int h, halide_type_t halide_type) {
    T *mem = NULL;
#ifdef __APPLE__
    // memalign() isn't present on OSX, but posix_memalign is
    int result = posix_memalign((void **)&mem, 128, w * h * sizeof(T));
    if (result != 0 || mem == NULL) {
        exit(1);
    }
#else
    mem = (T *)memalign(128, w * h * sizeof(T));
    if (mem == NULL) {
        exit(1);
    }
#endif

    halide_buffer_t buf = {0};
    buf.dim = (halide_dimension_t *)malloc(sizeof(halide_dimension_t) * 2);
    buf.host = (uint8_t *)mem;
    buf.dim[0].extent = w;
    buf.dim[1].extent = h;
    buf.type = halide_type;
    buf.dim[0].stride = 1;
    buf.dim[1].stride = w;
    buf.dim[0].min = -128;
    buf.dim[1].min = 0;

    for (int i = 0; i < w * h; i++) {
        mem[i] = rand_value<T>();
    }

    return buf;
}

#include "filters.h"

int main(int argc, char **argv) {
    const int W = 1024, H = 128;
    bool error = false;
    // Make some input buffers
    halide_buffer_t bufs[] = {
        make_buffer<float>(W, H, halide_type_of<float>()),
        make_buffer<double>(W, H, halide_type_of<double>()),
        make_buffer<uint16_t>(W, H, halide_type_t(halide_type_float, 16)),
        make_buffer<uint16_t>(W, H, halide_type_t(halide_type_bfloat, 16)),
        make_buffer<int8_t>(W, H, halide_type_of<int8_t>()),
        make_buffer<uint8_t>(W, H, halide_type_of<uint8_t>()),
        make_buffer<int16_t>(W, H, halide_type_of<int16_t>()),
        make_buffer<uint16_t>(W, H, halide_type_of<uint16_t>()),
        make_buffer<int32_t>(W, H, halide_type_of<int32_t>()),
        make_buffer<uint32_t>(W, H, halide_type_of<uint32_t>()),
        make_buffer<int64_t>(W, H, halide_type_of<int64_t>()),
        make_buffer<uint64_t>(W, H, halide_type_of<uint64_t>())};

    halide_buffer_t out = make_buffer<double>(1, 1, halide_type_of<double>());

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
             bufs + 10,
             bufs + 11,
             &out);
        if (*out_value) {
            printf("Error: %f\n", *out_value);
            error = true;
        }
    }

    for (int i = 0; i < sizeof(bufs) / sizeof(halide_buffer_t); i++) {
        free(bufs[i].dim);
        free(bufs[i].host);
    }
    free(out.dim);
    free(out.host);

    if (!error) {
        printf("Success!\n");
        return 0;
    } else {
        printf("Error occurred\n");
        return -1;
    }
}
