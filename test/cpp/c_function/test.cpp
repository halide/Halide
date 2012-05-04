#include "Halide.h"
#include <dlfcn.h>

using namespace Halide;

int call_counter = 0;

// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols

extern "C" {
float my_func(int x, float y) {
    call_counter++;
    printf("Hi: %d %f\n", x, y);
    return x*y;
}
}


int main(int argc, char **argv) {
    Var x, y;
    Func f;

    printf("Defining function...\n");

    f(x, y) = builtin(Float(32), "my_func", x, cast<float>(y));

    printf("Realizing function...\n");

    Image<float> imf = f.realize(32, 32);

    // Check the result was what we expected
    for (size_t i = 0; i < 32; i++) {
        for (size_t j = 0; j < 32; j++) {
            float correct = (float)(i*j);
            if (imf(i, j) != correct) {
                printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                return -1;
            }
        }
    }

    if (call_counter != 32*32) {
        printf("C function was called %d times instead of %d\n", call_counter, 32*32);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
