#include <stdio.h>
#include <Halide.h>
#include <iostream>

using namespace Halide;

bool validate(const Image<float> &im, float add)
{
    // Check the result was what we expected
    for (int i = 0; i < im.width(); i++) {
        for (int j = 0; j < im.height(); j++) {
            float correct = i*j + add;
            if (fabs(im(i, j) - correct) > 0.001f) {
                printf("im[%d, %d] = %f instead of %f\n", i, j, im(i, j), correct);
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {

    Var x("x"), y("y");
    Func f("f");

    printf("Defining function f...\n");

    f(x, y) = x*y + 1.0f;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, 8, 8);
    }

    {
        printf("Realizing function f...\n");

        Image<float> imf = f.realize(32, 32, target);
        if (!validate(imf, 1.0f)) {
            return -1;
        }
    }

    // Create (and destroy) a new function g.
    {
        Func g("g");

        printf("Defining function g...\n");

        g(x, y) = x*y + 2.0f;

        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 8, 8);
        }

        printf("Realizing function g...\n");

        Image<float> img = g.realize(32, 32, target);
        if (!validate(img, 2.0f)) {
            return -1;
        }
    }

    // Try using f again to ensure it is still valid (after g's destruction).
    printf("Realizing function f again...\n");

    Image<float> imf2 = f.realize(32, 32, target);
    if (!validate(imf2, 1.0f)) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
