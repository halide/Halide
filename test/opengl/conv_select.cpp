// test case provided by Lee Yuguang


#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    // Define the input
    const int width = 10, height = 10, channels = 4, res_channels = 2;
    Buffer<float> input(width, height, channels);
    for (int c = 0; c < input.channels(); c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = float(x + y);
            }
        }
    }

    // Define the algorithm.
    Var x, y, c;
    RDom r(0, 2, "r");
    Func f, g;

    Expr coordx = clamp(x + r, 0, input.width() - 1);
    f(x, y, c) = cast<float>(sum(input(coordx, y, c)));

    Expr R = select(f(x, y, c) > 9.0f, 1.0f, 0.0f);
    Expr G = select(f(x, y, c) > 9.0f, 0.f, 1.0f);
    g(x, y, c) = select(c==0, R, G);

    // Schedule f and g to compute in separate passes on the GPU.
    g.bound(c, 0, 2).glsl(x, y, c);

    // Generate the result.
    Buffer<float> result = g.realize(width, height, res_channels, target);
    result.copy_to_host();

    //Check the result.
    for (int c = 0; c < result.channels(); c++) {
        for (int y = 0; y < result.height(); y++) {
            for (int x = 0; x < result.width(); x++) {
                float temp = ((x + y)>4)?1.0f:0.0f;

                float correct = (c==0)? temp : (1.0f - temp);
                if (result(x, y, c) != correct) {
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
