#include "Halide.h"
#include <stdio.h>

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// out(x) = in(x) * x;
extern "C" DLLEXPORT int extern_stage(buffer_t *in, buffer_t *out) {
    assert(in->elem_size == 4);
    assert(out->elem_size == 4);
    if (in->host == NULL || out->host == NULL) {
        // We require input size = output size, and just for fun,
        // we'll require that the output size must be a multiple of 17

        /*
        printf("Query with in: %d %d, out: %d %d\n",
               in->min[0], in->extent[0],
               out->min[0], out->extent[0]);
        */

        if (out->host == NULL) {
            out->extent[0] = ((out->extent[0] + 16)/17)*17;
        }
        if (in->host == NULL) {
            in->extent[0] = out->extent[0];
            in->min[0] = out->min[0];
        }

        /*
        printf("Query result in: %d %d, out: %d %d\n",
               in->min[0], in->extent[0],
               out->min[0], out->extent[0]);
        */
    } else {
        assert(out->extent[0] % 17 == 0);
        printf("in: %d %d, out: %d %d\n",
               in->min[0], in->extent[0],
               out->min[0], out->extent[0]);
        int32_t *in_origin = (int32_t *)in->host - in->min[0];
        int32_t *out_origin = (int32_t *)out->host - out->min[0];
        for (int i = out->min[0]; i < out->min[0] + out->extent[0]; i++) {
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

        Image<int32_t> result = h.realize(100);

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
