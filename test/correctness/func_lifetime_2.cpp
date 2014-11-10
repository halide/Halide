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

    Func g("g");

    Target target = get_jit_target_from_environment();

    {
        printf("Defining function f...\n");
        Func f("f");

        f(x, y) = x*y + 1.0f;

        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, 8, 8, Device_Default_GPU);
        }

        {
            printf("Realizing function f...\n");

            Image<float> imf = f.realize(32, 32, target);
            if (!validate(imf, 1.0f)) {
                return -1;
            }
        }

        printf("Defining function g...\n");

        g(x, y) = x*y + 2.0f;

        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 8, 8, Device_Default_GPU);
        }

        printf("Realizing function g...\n");

        Image<float> img1 = g.realize(32, 32, target);
        if (!validate(img1, 2.0f)) {
            return -1;
        }
    }

    // Try using g again to ensure it is still valid (after f's destruction).
    printf("Realizing function g again...\n");

    Image<float> img2 = g.realize(32, 32, target);
    if (!validate(img2, 2.0f)) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
