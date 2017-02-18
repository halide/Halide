#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    // Define the input.
    const int width = 10, height = 10, channels = 3;
    Buffer<float> input(width, height, channels);
    input.fill([](int x, int y, int c) {
        return x + y;
    });

    // Define the algorithm.
    Var x, y, c;
    RDom r(0, 3, "r");
    Func g;

    g(x, y, c) = sum(input(x, y, r));

    // Schedule f and g to compute in separate passes on the GPU.
    g.bound(c, 0, 3).glsl(x, y, c);

    // Generate the result.
    Buffer<float> result = g.realize(10, 10, 3, target);
    result.copy_to_host();

    // Check the result.
    if (!Testing::check_result<float>(result, 1e-6, [](int x, int y, int c) { return 3.0f * (x + y); })) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
