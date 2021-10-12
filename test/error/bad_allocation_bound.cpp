#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y) * 2;

    f.compute_at(g, y);
    f.bound_allocation(3);
    g.realize({10, 10});

    printf("Success!\n");
    return 0;
}
