#include "Halide.h"
#include <stdio.h>

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int flip_x(buffer_t *in1, buffer_t *in2, buffer_t *out) {
    int min = out->min[0];
    int max = out->min[0] + out->extent[0] - 1;

    int extent = out->extent[0];
    int flipped_min = -max;
    int flipped_max = -min;

    if (in1->host == NULL || in2->host == NULL) {
        // If any of the inputs have a null host pointer, we're in
        // bounds inference mode, and should mutate those input
        // buffers that have a null host pointer.
        printf("Doing flip_x bounds inference over [%d %d]\n", min, max);
        if (in1->host == NULL) {
            in1->min[0] = flipped_min;
            in1->extent[0] = extent;
        }
        if (in2->host == NULL) {
            in2->min[0] = flipped_min;
            in2->extent[0] = extent;
        }
        // We don't mutate the output buffer, because we can handle
        // any size output.

        //printf("Bounds inference flip_x over [%d %d] requires [%d %d]\n", min, extent, flipped_min, extent);
    } else {
        assert(in1->elem_size == 1);
        assert(in2->elem_size == 4);
        assert(out->elem_size == 1);

        printf("Computing flip_x over [%d %d]\n", min, max);

        // Check the inputs are as large as we expected. They should
        // be, if the above bounds inference code is right.
        assert(in1->min[0] <= flipped_min &&
               in1->min[0] + in1->extent[0] > flipped_max);
        assert(in2->min[0] <= flipped_min &&
               in2->min[0] + in2->extent[0] > flipped_max);

        // Check the strides are what we want.
        assert(in1->stride[0] == 1 && in2->stride[0] == 1 && out->stride[0] == 1);

        // Get pointers to the origin from each of the inputs (because
        // we're flipping about the origin)
        uint8_t *dst = (uint8_t *)(out->host) - out->min[0];
        uint8_t *src1 = (uint8_t *)(in1->host) - in1->min[0];
        int *src2 = (int *)(in2->host) - in2->min[0];

        // Do the flip.
        for (int i = min; i <= max; i++) {
            dst[i] = src1[-i] + src2[-i];
        }
    }

    return 0;
}

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        // TODO: Add JavaScript extern support.
        printf("Skipping extern_stage test for JavaScript as it uses a C extern function.\n");
        return 0;
    }

    Func f, g, h;
    Var x;

    // Make some input data in the range [-99, 0]
    Image<uint8_t> input(100);
    input.set_min(-99);
    lambda(x, cast<uint8_t>(x*x)).realize(input);

    assert(input(-99) == (uint8_t)(-99*-99));

    f(x) = x*x;

    std::vector<ExternFuncArgument> args(2);
    args[0] = input;
    args[1] = f;
    g.define_extern("flip_x", args, UInt(8), 1);

    h(x) = g(x) * 2;

    f.compute_at(h, x);
    g.compute_at(h, x);
    Var xi;
    h.vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);

    Image<uint8_t> result = h.realize(100);

    for (int i = 0; i < 100; i++) {
        uint8_t correct = 4*i*i;
        if (result(i) != correct) {
            printf("result(%d) = %d instead of %d\n", i, result(i), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
