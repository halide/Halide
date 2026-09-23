#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), xi("xi");

    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x + 1, y);
    f.compute_at(g, y)
        .store_root()
        .split_storage(x, xo, xi, 4)
        .fold_storage(xi, 4);

    g.realize({16, 16});

    return 0;
}
