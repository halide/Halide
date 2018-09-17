#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g"), h("h"), input("input");
    Var x("x"), y("y");

    input(x, y) = x + y;
    f(x, y) = input(x, y);
    f(x, y) += 5;
    g(x, y) = input(x, y);
    g(x, y) += 10;
    h(x, y) = x*y;
    h(x, y) += 2*x;

    input.compute_root();
    h.compute_with(g.update(), y);
    h.update().compute_with(g, y);

	Pipeline({f, g, h}).realize(10, 10);

    return 0;
}
