#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    g(x) = f(x);
    f.compute_root().split_storage(x, xo, xi, 4);
    g.prefetch(f, x, x, 8);

    g.realize({16});

    return 0;
}
