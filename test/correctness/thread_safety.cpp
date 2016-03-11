#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Test if the compiler itself is thread-safe. This test is
    // intended to be run in a thread-sanitizer with openmp turned on.
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int i = 0; i < 1000; i++) {
        Func f;
        Var x;
        f(x) = x;
        f.realize(100);
    }

    printf("Success!\n");

    return 0;
}
