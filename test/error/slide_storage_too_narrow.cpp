#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y"), yo("yo"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x, y - 1);

    g.split(y, yo, yi, 4);
    // y is spread across yo and yi, but f's storage only lives for one
    // iteration of yo, so sliding over y would read values that have been
    // thrown away.
    f.store_at(g, yo).compute_at(g, x).slide(g, y);

    g.realize({8, 16});

    printf("Success!\n");
    return 0;
}
