#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL)) {
        fprintf(stderr, "ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }


    // Define the input.
    const int width = 10, height = 10, channels = 3;
    Image<float> input(width, height, channels);
    for (int c = 0; c < input.channels(); c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = x + y;
            }
        }
    }

    // Define the algorithm.
    Var x, y, c;
    RDom r(0, 3, "r");
    Func g;

    g(x, y, c) = sum(input(x, y, r));

    // Schedule f and g to compute in separate passes on the GPU.
    g.bound(c, 0, 3).glsl(x, y, c);

    // Generate the result.
    Image<float> result = g.realize(10, 10, 3);
    result.copy_to_host();

    // Check the result.
    for (int c = 0; c < result.channels(); c++) {
        for (int y = 0; y < result.height(); y++) {
            for (int x = 0; x < result.width(); x++) {
                float correct = 3.0f * (x + y);
                if (fabs(result(x, y, c) - correct) > 1e-6) {
                    fprintf(stderr, "result(%d, %d, %d) = %f instead of %f\n",
                            x, y, c, result(x, y, c), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
