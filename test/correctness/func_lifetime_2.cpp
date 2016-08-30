#include <stdio.h>
#include "Halide.h"
#include <iostream>

using namespace Halide;

bool validate(const Image<int> &im, int add)
{
    // Check the result was what we expected
    for (int i = 0; i < im.width(); i++) {
        for (int j = 0; j < im.height(); j++) {
            int correct = i*j + add;
            if (im(i, j) != correct) {
                printf("im[%d, %d] = %d instead of %d\n", i, j, im(i, j), correct);
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

        f(x, y) = x*y + 1;

        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, 8, 8);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            f.hexagon().vectorize(x, 32);
        }

        {
            printf("Realizing function f...\n");

            Image<int> imf = f.realize(32, 32, target);
            if (!validate(imf, 1)) {
                return -1;
            }
        }

        printf("Defining function g...\n");

        g(x, y) = x*y + 2;

        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 8, 8);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            g.hexagon().vectorize(x, 32);
        }

        printf("Realizing function g...\n");

        Image<int> img1 = g.realize(32, 32, target);
        if (!validate(img1, 2)) {
            return -1;
        }
    }

    // Try using g again to ensure it is still valid (after f's destruction).
    printf("Realizing function g again...\n");

    Image<int> img2 = g.realize(32, 32, target);
    if (!validate(img2, 2.0f)) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
