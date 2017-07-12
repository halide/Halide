// test case provided by Lee Yuguang

#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    // Define the input
    const int width = 10, height = 10, channels = 4, res_channels = 2;
    Buffer<float> input(width, height, channels);
    input.fill([](int x, int y, int c) {
        return float(x + y);
    });

    // Define the algorithm.
    Var x, y, c;
    RDom r(0, 2, "r");
    Func f, g;

    Expr coordx = clamp(x + r, 0, input.width() - 1);
    f(x, y, c) = cast<float>(sum(input(coordx, y, c)));

    Expr R = select(f(x, y, c) > 9.0f, 1.0f, 0.0f);
    Expr G = select(f(x, y, c) > 9.0f, 0.f, 1.0f);
    g(x, y, c) = select(c == 0, R, G);

    // Schedule f and g to compute in separate passes on the GPU.
    g.bound(c, 0, 2).glsl(x, y, c);

    // Generate the result.
    Buffer<float> result = g.realize(width, height, res_channels, target);
    result.copy_to_host();

    //Check the result.
    if (!Testing::check_result<float>(result, [](int x, int y, int c) {
            const float temp = ((x + y) > 4) ? 1.0f : 0.0f;
            return (c == 0) ? temp : (1.0f - temp);
        })) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
