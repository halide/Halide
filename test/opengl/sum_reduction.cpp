#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    // Define the input
    const int width = 10, height = 10, channels = 4;
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
    RDom r(0, 5, "r");
    Func g;
    Expr coordx = clamp(x + r, 0, input.width() - 1);
    g(x, y, c) = cast<float>( sum(input(coordx, y, c)) / sum(r) * 255.0f );

    // Schedule f and g to compute in separate passes on the GPU.
    g.bound(c, 0, 4).glsl(x, y, c);

    // Generate the result.
    Buffer<float> result = g.realize(width, height, channels, target);
    result.copy_to_host();

    // Check the result.
    for (int c = 0; c < result.channels(); c++) {
        for (int y = 0; y < result.height(); y++) {
            for (int x = 0; x < result.width(); x++) {
                float temp = 0.0f;
                for (int r = 0; r < 5; r++){
                    temp += input(std::min(x+r, input.width()-1), y, c);
                }
                float correct = temp / 10.0f * 255.0f;
                if (fabs(result(x, y, c) - correct) > 1e-3) {
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
