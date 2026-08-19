#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    g(x) = f(x) + f(x - 1);

    // xo innermost, so x runs 0, 8, 16, ... and then 1, 9, 17, ...
    g.split(x, xo, xi, 8).reorder(xo, xi);
    f.store_root().compute_at(g, xo).slide(g, x);

    g.realize({32});

    printf("Success!\n");
    return 0;
}
