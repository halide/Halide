#include "Halide.h"
#include <stdio.h>

void dump_buffer_shape(halide_buffer_t *b) {
    for (int i = 0; i < b->dimensions; i++) {
        printf(" %d %d %d\n", b->dim[i].min, b->dim[i].extent, b->dim[i].stride);
    }
}

extern "C" HALIDE_EXPORT_SYMBOL int copy(halide_buffer_t *in, halide_buffer_t *out) {

    /*
    printf("out:\n");
    dump_buffer_shape(out);
    printf("in:\n");
    dump_buffer_shape(in);
    */

    if (in->is_bounds_query()) {
        // Give it the same shape as the output
        in->dim[0] = out->dim[0];
        in->dim[1] = out->dim[1];
    } else {
        // Check the sizes and strides match. This is not guaranteed
        // by the interface, but it should happen with this schedule
        // because we compute the input to the extern stage at the
        // same granularity as the extern stage.

        assert(in->dim[0] == out->dim[0]);
        assert(in->dim[1] == out->dim[1]);

        size_t sz = out->type.bytes() * out->dim[0].extent * out->dim[1].extent;

        // Make sure we can safely do a dense memcpy. Should be true because the extent..
        assert(out->dim[0].stride == 1 && out->dim[1].stride == out->dim[0].extent);

        memcpy(out->host, in->host, sz);
    }

    return 0;
}

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    f(x, y) = x * x + y;

    // Name of the function and the args, then types of the outputs, then dimensionality
    g.define_extern("copy", {f}, Int(32), 2);

    RDom r(0, 100);
    h(x, y) += r * (g(x, y) - f(x, y));

    f.compute_at(h, y);
    g.compute_at(h, y).store_root();

    Buffer<int> result = h.realize({10, 10});

    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            uint8_t correct = 0;
            if (result(x, y) != 0) {
                printf("result(%d, %d) = %d instead of %d\n", x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
