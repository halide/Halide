#include <stdio.h>
#include "Halide.h"

using namespace Halide;

// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols
// This is not supported by the C backend.

// On windows, you need to use declspec to do the same.
#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int call_counter = 0;
extern "C" DLLEXPORT float my_func(int x, float y) {
    call_counter++;
    return x*y;
}
HalideExtern_2(float, my_func, int, float);

int call_counter2 = 0;
extern "C" DLLEXPORT float my_func2(int x, float y) {
    call_counter2++;
    return x*y;
}

int main(int argc, char **argv) {
    Var x, y;
    Func f;

    f(x, y) = my_func(x, cast<float>(y));

    Image<float> imf = f.realize(32, 32);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (float)(i*j);
            float delta = imf(i, j) - correct;
            if (delta < -0.001 || delta > 0.001) {
                printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                return -1;
            }
        }
    }

    if (call_counter != 32*32) {
        printf("C function my_func was called %d times instead of %d\n", call_counter, 32*32);
        return -1;
    }

    Func g;
    g(x, y) = my_func(x, cast<float>(y));

    Image<float> imf2 = g.realize(32, 32, get_jit_target_from_environment(), { { "my_func", my_func2 } });

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (float)(i*j);
            float delta = imf2(i, j) - correct;
            if (delta < -0.001 || delta > 0.001) {
                printf("imf2[%d, %d] = %f instead of %f\n", i, j, imf2(i, j), correct);
                return -1;
            }
        }
    }

    if (call_counter2 != 32*32) {
        printf("C function my_func2 was called %d times instead of %d\n", call_counter2, 32*32);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
