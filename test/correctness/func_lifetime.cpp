#include <stdio.h>
#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");
    Func f("f");

    printf("Defining function f...\n");

    f(x, y) = x*y + 2.4f;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, 8, 8, GPU_Default);
    }

    {
        printf("Realizing function f...\n");

        Image<float> imf = f.realize(32, 32, target);

        // Check the result was what we expected
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                float correct = i*j + 2.4f;
                if (fabs(imf(i, j) - correct) > 0.001f) {
                    printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                    return -1;
                }
            }
        }
    }

    // Create (and destroy) a new function g.
    {
        Func g("g");

        printf("Defining function g...\n");

        g(x, y) = 2.0f*(x*y + 2.4f);

        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 8, 8, GPU_Default);
        }

        printf("Realizing function g...\n");

        Image<float> img = g.realize(32, 32, target);

        // Check the result was what we expected
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                float correct = 2.0f*(i*j + 2.4f);
                if (fabs(img(i, j) - correct) > 0.001f) {
                    printf("imf[%d, %d] = %f instead of %f\n", i, j, img(i, j), correct);
                    return -1;
                }
            }
        }
    }

    printf("Realizing function f again...\n");

    Image<float> imf2 = f.realize(32, 32, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = i*j + 2.4f;
            if (fabs(imf2(i, j) - correct) > 0.001f) {
                printf("imf2[%d, %d] = %f instead of %f\n", i, j, imf2(i, j), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
