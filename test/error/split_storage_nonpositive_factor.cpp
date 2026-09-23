#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), xo("xo"), xi("xi");
    Param<int> factor("factor");

    f(x) = x;
    g(x) = f(x);
    f.compute_root().split_storage(x, xo, xi, factor);

    factor.set(0);
    g.realize({16});

    return 0;
}
