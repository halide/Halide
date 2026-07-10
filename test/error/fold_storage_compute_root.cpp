#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Func f, g;

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    f.compute_root().fold_storage(y, 4);

    Buffer<int> im = g.realize({10, 10});

    printf("Success!\n");
    return 0;
}
