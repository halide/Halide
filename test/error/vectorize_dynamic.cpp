#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Buffer<int> input(5, 5);
    Func f;
    f(x, y) = input(x, y) * 2;
    Var xo, xi;

    Param<int> vector_size;

    // You can only vectorize across compile-time-constant sizes.
    f.split(x, xo, xi, vector_size).vectorize(xi);

    // Should result in an error
    vector_size.set(4);
    Buffer<int> out = f.realize(5, 5);

    printf("Success!\n");
    return 0;
}
