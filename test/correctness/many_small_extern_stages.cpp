#include "Halide.h"
#include <stdio.h>

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int copy(buffer_t *in, buffer_t *out) {
    /*
    printf("out: %d %d %d %d   %d %d %d %d   %d %d %d %d\n",
           out->min[0], out->min[1], out->min[2], out->min[3],
           out->stride[0], out->stride[1], out->stride[2], out->stride[3],
           out->extent[0], out->extent[1], out->extent[2], out->extent[3]);
    printf("in: %d %d %d %d   %d %d %d %d   %d %d %d %d\n",
           in->min[0], in->min[1], in->min[2], in->min[3],
           in->stride[0], in->stride[1], in->stride[2], in->stride[3],
           in->extent[0], in->extent[1], in->extent[2], in->extent[3]);
    */
    if (in->host == NULL) {
        // Give it all the same metadata
        (*in) = (*out);
        in->host = NULL;
        in->dev = 0;
        in->host_dirty = false;
        in->dev_dirty = false;
    } else {
        // Check the sizes and strides match. This is not guaranteed
        // by the interface, but it should happen with this schedule
        // because we compute the input to the extern stage at the
        // same granularity as the extern stage.

        assert(in->extent[0] == out->extent[0]);
        assert(in->extent[1] == out->extent[1]);
        assert(in->extent[2] == out->extent[2]);
        assert(in->extent[3] == out->extent[3]);
        assert(in->min[0] == out->min[0]);
        assert(in->min[1] == out->min[1]);
        assert(in->min[2] == out->min[2]);
        assert(in->min[3] == out->min[3]);
        assert(in->stride[0] == out->stride[0]);
        assert(in->stride[1] == out->stride[1]);
        assert(in->stride[2] == out->stride[2]);
        assert(in->stride[3] == out->stride[3]);
        size_t sz = out->elem_size;
        if (out->extent[0]) sz *= out->extent[0];
        if (out->extent[1]) sz *= out->extent[1];
        if (out->extent[2]) sz *= out->extent[2];
        if (out->extent[3]) sz *= out->extent[3];

        // Make sure we can safely do a dense memcpy. Should be true because the extent..
        if (out->extent[0]) assert(out->stride[0] == 1);
        if (out->extent[1]) assert(out->stride[1] == out->extent[0]);
        if (out->extent[2]) assert(out->stride[2] == out->extent[1] * out->stride[1]);
        if (out->extent[3]) assert(out->stride[3] == out->extent[2] * out->stride[2]);

        memcpy(out->host, in->host, sz);
    }

    return 0;
}

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    f(x, y) = x*x + y;

    // Name of the function and the args, then types of the outputs, then dimensionality
    g.define_extern("copy",
                    Internal::vec<ExternFuncArgument>(f),
                    Int(32), 2);

    RDom r(0, 100);
    h(x, y) += r * (g(x, y) - f(x, y));

    f.compute_at(h, y);
    g.compute_at(h, y).store_root();

    Image<int> result = h.realize(10, 10);

    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            uint8_t correct = 0;
            if (result(x, y) != 0) {
                printf("result(%d, %d) = %d instead of %d\n", x, y, result(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
