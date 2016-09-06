// test case provided by Lee Yuguang


#include "Halide.h"
#include <stdio.h>
#include <chrono>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL)) {
        fprintf(stderr, "ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    // Define the input
    const int width = 10, height = 12, channels = 2, res_channels = 2;
    Image<float> input(width, height, channels);
    for (int c = 0; c < input.channels(); c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = float(x + y);
            }
        }
    }

    // Define the algorithm.
    Var x, y, c;
    RDom r(3, 5, "r");
    Func f, g;

    Expr coordx = clamp(x + r, 0, input.width() - 1);
    f(x, y, c) = cast<uint8_t>(0);
    f(r.x, y, c) = cast<uint8_t>(11);


    // Schedule f and g to compute in separate passes on the GPU.
    f.bound(c, 0, 2);
    f.update(0).glsl(r.x, y, c);

    // Generate the result.
    Image<uint8_t> result = f.realize(width, height, res_channels);


    result.copy_to_host();

    //Check the result.
    for (int c = 0; c < result.channels(); c++) {
        for (int y = 0; y < result.height(); y++) {
            for (int x = 0; x < result.width(); x++) {
                uint8_t correct = (x>=3 && x<8) ? 11 : 0;
                if (result(x, y, c) != correct) {
                    fprintf(stderr, "result(%d, %d, %d) = %d, should be %d\n",
                            x, y, c, result(x, y, c), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
