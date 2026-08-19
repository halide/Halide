#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), xo("xo"), xi("xi");

    Func f("f"), g("g");
    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x - 1, y);

    g.split(x, xo, xi, 4);
    // x and xo both move the window along f's first dimension, so only one of
    // them could take effect.
    f.store_root().compute_at(g, xi).slide(g, x).slide(g, xo);

    g.realize({16, 16});

    printf("Success!\n");
    return 0;
}
