#include "Halide.h"
#include <stdio.h>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// out(x) = in(x) * x;
extern "C" DLLEXPORT int extern_stage(halide_buffer_t *in, halide_buffer_t *out) {
    assert(in->type == halide_type_of<int>());
    assert(out->type == halide_type_of<int>());
    if (in->host == nullptr || out->host == nullptr) {
        // We require input size = output size, and just for fun,
        // we'll require that the output size must be a multiple of 17

        if (out->host == nullptr) {
            out->dim[0].extent = ((out->dim[0].extent + 16)/17)*17;
        }
        if (in->host == nullptr) {
            in->dim[0].extent = out->dim[0].extent;
            in->dim[0].min = out->dim[0].min;
        }

    } else {
        assert(out->dim[0].extent % 17 == 0);
        printf("in: %d %d, out: %d %d\n",
               in->dim[0].min, in->dim[0].extent,
               out->dim[0].min, out->dim[0].extent);
        int32_t *in_origin = (int32_t *)in->host - in->dim[0].min;
        int32_t *out_origin = (int32_t *)out->host - out->dim[0].min;
        for (int i = out->dim[0].min; i < out->dim[0].min + out->dim[0].extent; i++) {
            out_origin[i] = in_origin[i] * i;
        }
    }
    return 0;
}

using namespace Halide;

int main(int argc, char **argv) {

    // We have two variants we want to test
    for (int i = 0; i < 2; i++) {
        Func f, g, h;
        Var x;
        f(x) = x*x;

        g.define_extern("extern_stage", {f}, Int(32), 1);

        h(x) = g(x) * 2;

        // Compute h in 10-wide sections
        Var xo;
        h.split(x, xo, x, 10);
        f.compute_root();
        if (i == 0) {
            g.compute_at(h, xo);
        } else {
            g.compute_root();
        }

        Buffer<int32_t> result = h.realize(100);

        for (int i = 0; i < 100; i++) {
            int32_t correct = i*i*i*2;
            if (result(i) != correct) {
                printf("result(%d) = %d instead of %d\n", i, result(i), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
