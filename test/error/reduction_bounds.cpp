#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x");
    RDom r(0, 100, "r");

    f(x) = x;

    g(x) = 0;
    g(x) = f(g(x-1)) + r;

    f.compute_at(g, r.x);

    // Use of f is unbounded in g.

    g.realize(100);

    return 0;
}
