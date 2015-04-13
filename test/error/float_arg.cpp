#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;
    f(x, y) = 3*x + y;

    // Should result in an error
    Func g;
    g(x) = f(f(x, 3) * 17.0f, 3);

    printf("Success!\n");
    return 0;
}

