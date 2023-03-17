#include "Halide.h"

using namespace Halide;

// See https://github.com/halide/Halide/issues/3061

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    if (t.has_feature(Target::OpenGLCompute)) {
        printf("[SKIP] No support for vector loads and stores in OpenGLCompute yet\n");
        // https://github.com/halide/Halide/issues/4979
        return 0;
    }

    // Fill input buffer.
    Buffer<float> input(2, 2, 3);
    Buffer<float> output(2, 2, 3);
    float *input_data = input.data();
    for (int i = 0; i < 12; ++i) {
        input_data[i] = i;
    }
    input.set_host_dirty();

    // Define a function.
    Var x("x"), y("y"), c("c"), xo("xo"), xi("xi"), yo("yo"), yi("yi"), co("co"), ci("ci"), n("n");
    Func func("func");
    RDom r(0, 1, 0, 1);
    func(x, y, c) = sum(input(x + r.x, y + r.y, c));

    // Schedule.
    func
        .bound(x, 0, 2)
        .bound(y, 0, 2)
        .bound(c, 0, 3)
        .split(x, xo, xi, 2)
        .split(y, yo, yi, 2)
        .split(c, co, ci, 3)
        .gpu_blocks(xo, yo, co)
        .gpu_threads(xi, yi)
        .reorder(xi, yi, ci, xo, yo, co)
        .vectorize(ci);

    func.realize(output);

    // Print output.
    output.copy_to_host();

    float *output_data = output.data();
    for (int i = 0; i < 12; ++i) {
        if (input_data[i] != output_data[i]) {
            printf("output(%d) = %f instead of %f\n", i, output_data[i], input_data[i]);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
