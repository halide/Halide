#include "Halide.h"
#include <stdio.h>

extern "C" HALIDE_EXPORT_SYMBOL int flip_x(halide_buffer_t *in1, halide_buffer_t *in2, halide_buffer_t *out) {
    int min = out->dim[0].min;
    int max = out->dim[0].min + out->dim[0].extent - 1;

    int extent = out->dim[0].extent;
    int flipped_min = -max;
    int flipped_max = -min;

    if (in1->host == nullptr || in2->host == nullptr) {
        // If any of the inputs have a null host pointer, we're in
        // bounds inference mode, and should mutate those input
        // buffers that have a null host pointer.
        printf("Doing flip_x bounds inference over [%d %d]\n", min, max);
        if (in1->is_bounds_query()) {
            in1->dim[0].min = flipped_min;
            in1->dim[0].extent = extent;
        }
        if (in2->is_bounds_query()) {
            in2->dim[0].min = flipped_min;
            in2->dim[0].extent = extent;
        }
        // We don't mutate the output buffer, because we can handle
        // any size output.

        // printf("Bounds inference flip_x over [%d %d] requires [%d %d]\n", min, extent, flipped_min, extent);
    } else {
        assert(in1->type == halide_type_of<uint8_t>());
        assert(in2->type == halide_type_of<int32_t>());
        assert(out->type == halide_type_of<uint8_t>());

        printf("Computing flip_x over [%d %d]\n", min, max);

        // Check the inputs are as large as we expected. They should
        // be, if the above bounds inference code is right.
        assert(in1->dim[0].min <= flipped_min &&
               in1->dim[0].min + in1->dim[0].extent > flipped_max);
        assert(in2->dim[0].min <= flipped_min &&
               in2->dim[0].min + in2->dim[0].extent > flipped_max);

        // Check the strides are what we want.
        assert(in1->dim[0].stride == 1 && in2->dim[0].stride == 1 && out->dim[0].stride == 1);

        // Get pointers to the origin from each of the inputs (because
        // we're flipping about the origin)
        uint8_t *dst = (uint8_t *)(out->host) - out->dim[0].min;
        uint8_t *src1 = (uint8_t *)(in1->host) - in1->dim[0].min;
        int *src2 = (int *)(in2->host) - in2->dim[0].min;

        // Do the flip.
        for (int i = min; i <= max; i++) {
            dst[i] = src1[-i] + src2[-i];
        }
    }

    return 0;
}

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h;
    Var x;

    // Make some input data in the range [-99, 0]
    Buffer<uint8_t> input(100);
    input.set_min(-99);
    lambda(x, cast<uint8_t>(x * x)).realize(input);

    assert(input(-99) == (uint8_t)(-99 * -99));

    f(x) = x * x;

    std::vector<ExternFuncArgument> args(2);
    args[0] = input;
    args[1] = f;
    g.define_extern("flip_x", args, UInt(8), 1);

    h(x) = g(x) * 2;

    f.compute_at(h, x);
    g.compute_at(h, x);
    Var xi;
    h.vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);

    Buffer<uint8_t> result = h.realize({100});

    for (int i = 0; i < 100; i++) {
        uint8_t correct = 4 * i * i;
        if (result(i) != correct) {
            printf("result(%d) = %d instead of %d\n", i, result(i), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
