#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g");

    f(x, y) = x + y;
    f(x, y) += 1;

    g(x, y) = x - y;
    g(x, y) += 1;

    f.compute_root();
    g.compute_root();

    f.compute_with(g.update(0), y);
    f.update(0).compute_with(g, y);

    Pipeline p({f, g});
    p.realize(200, 200);

    return 0;
}