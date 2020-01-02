#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, c;

    Func f, g;

    f(x, y) = x;
    g(x, y) = f(x - 1, y + 1) + f(x, y - 1);
    f.store_root().compute_at(g, y).fold_storage(y, 2);

    Buffer<int> im = g.realize(100, 1000);

    printf("Should have gotten a bad fold!\n");
    return -1;
}
