#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "pipeline_c.h"
#include "pipeline_native.h"

using namespace Halide;

extern "C" int an_extern_func(int x, int y) {
    return x + y;
}

extern "C" int an_extern_stage(buffer_t *in, buffer_t *out) {
    if (in->host == nullptr) {
        // We expect a 2D input.
        in->extent[0] = 10;
        in->extent[1] = 10;
        in->min[0] = 0;
        in->min[1] = 0;
    } else {
        assert(out->host);
        int result = 0;
        int16_t *origin = (int16_t *)in->host;
        origin -= in->min[0] * in->stride[0];
        origin -= in->min[1] * in->stride[1];
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                result += origin[x * in->stride[0] + y * in->stride[1]];
            }
        }
        int16_t *dst = (int16_t *)(out->host);
        dst[0] = result;
    }
    return 0;
}

int main(int argc, char **argv) {
    Buffer<uint16_t> in(1432, 324);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = (uint16_t)rand();
        }
    }

    Buffer<uint16_t> out_native(423, 633);
    Buffer<uint16_t> out_c(423, 633);

    pipeline_native(in, out_native);

    pipeline_c(in, out_c);

    for (int y = 0; y < out_native.height(); y++) {
        for (int x = 0; x < out_native.width(); x++) {
            if (out_native(x, y) != out_c(x, y)) {
                printf("out_native(%d, %d) = %d, but out_c(%d, %d) = %d\n",
                       x, y, out_native(x, y),
                       x, y, out_c(x, y));
            }
        }
    }

    printf("Success!\n");
    return 0;
}
