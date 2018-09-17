#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g"), input("input");
    Var x("x"), y("y");

    input(x, y) = x + y;
    f(x, y) = input(x, y);
    f(x, y) += 5;
    g(x, y) = input(x, y);
    g(x, y) += 10;

    input.compute_root();
    f.update().compute_with(g.update(), y);

    Pipeline({f, g}).realize(10, 10);

    return 0;
}
