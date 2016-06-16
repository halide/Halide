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
    Func f("f");

    printf("Defining function f...\n");

    f(x, y) = x*y + 1;

    Target target = get_jit_target_from_environment();
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

    // Create (and destroy) a new function g.
    {
        Func g("g");

        printf("Defining function g...\n");

        g(x, y) = x*y + 2;

        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 8, 8);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            g.hexagon().vectorize(x, 32);
        }

        printf("Realizing function g...\n");

        Image<int> img = g.realize(32, 32, target);
        if (!validate(img, 2)) {
            return -1;
        }
    }

    // Try using f again to ensure it is still valid (after g's destruction).
    printf("Realizing function f again...\n");

    Image<int> imf2 = f.realize(32, 32, target);
    if (!validate(imf2, 1)) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
