#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideImage.h"
#include "pipeline_c.h"
#include "pipeline_native.h"

using namespace Halide::Tools;

extern "C" int an_extern_func(int x, int y) {
    return x + y;
}

extern "C" int an_extern_stage(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->host == nullptr) {
        // We expect a 2D input.
        in->dim[0] = halide_dimension_t(0, 10, 1);
        in->dim[1] = halide_dimension_t(0, 10, 10);
    } else {
        assert(out->host);
        int result = 0;
        int16_t *origin = (int16_t *)in->host;
        origin -= in->dim[0].min * in->dim[0].stride;
        origin -= in->dim[1].min * in->dim[1].stride;
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                result += origin[x * in->dim[0].stride + y * in->dim[1].stride];
            }
        }
        int16_t *dst = (int16_t *)(out->host);
        dst[0] = result;
    }
    return 0;
}

int main(int argc, char **argv) {
    Image<uint16_t> in(1432, 324);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = (uint16_t)rand();
        }
    }

    Image<int16_t> out_native(423, 633);
    Image<int16_t> out_c(423, 633);

    pipeline_native(&in, &out_native);

    pipeline_c(&in, &out_c);

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
