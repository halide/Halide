#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f, g;
    Var x, y;

    g(x, y) = x + y;

    f(x, y) = g(x, x);

    f.vectorize(x, 4);

    Buffer<int> out = f.realize(4, 4);

    printf("Success!\n");

    return 0;
}
