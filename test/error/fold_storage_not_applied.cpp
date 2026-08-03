#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Func f, g;

    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x, y - 1);

    f.store_root().compute_at(g, y).fold_storage(x, 8);

    Buffer<int> im = g.realize({10, 10});

    printf("Success!\n");
    return 0;
}
