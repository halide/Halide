#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");

    Var x("x");

    f(x) = select(x <= 0, 0, f(x - 1) + f(x - 2) + x);
    g(x) = f(x) * 2;

    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.realize({10});

    printf("Success!\n");
    return 0;
}
