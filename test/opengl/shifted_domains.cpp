#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

// This test executes a simple kernel with a non-zero min value. The code is
// adapted from lesson_06_realizing_over_shifted_domains.cpp and scheduled for
// GLSL
int shifted_domains() {

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    int errors = 0;

    Func gradient("gradient");
    Var x("x"), y("y"), c("c");
    gradient(x, y, c) = cast<float>(x + y);

    gradient.bound(c, 0, 1);
    gradient.glsl(x, y, c);

    printf("Evaluating gradient from (0, 0) to (7, 7)\n");
    Buffer<float> result(8, 8, 1);
    gradient.realize(result, target);
    result.copy_to_host();

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float actual = result(x, y);
            float expected = float(x + y);
            if (actual != expected) {
                printf("Error at %d,%d result %f should be %f\n",
                       x, y, actual, expected);
                errors++;
            }
        }
    }

    Buffer<float> shifted(5, 7, 1);
    shifted.set_min(100, 50);

    printf("Evaluating gradient from (100, 50) to (104, 56)\n");

    gradient.realize(shifted, target);
    shifted.copy_to_host();

    for (int y = 50; y < 57; y++) {
        for (int x = 100; x < 105; x++) {
            float actual = shifted(x, y);
            float expected = float(x + y);
            if (actual != expected) {
                printf("Error at %d,%d result %f should be %f\n",
                       x, y, actual, expected);
                errors++;
            }
        }
    }

    // Test with a negative min
    shifted.set_min(-100, -50);

    printf("Evaluating gradient from (-100, -50) to (-96, -44)\n");

    gradient.realize(shifted, target);
    shifted.copy_to_host();

    for (int y = -50; y < -44; y++) {
        for (int x = -100; x < -96; x++) {
            float actual = shifted(x, y);
            float expected = float(x + y);
            if (actual != expected) {
                printf("Error at %d,%d result %f should be %f\n",
                       x, y, actual, expected);
                errors++;
            }
        }
    }

    return errors;
}

int main() {

    if (shifted_domains() == 0) {
        printf("PASSED\n");
    }

    return 0;
}
