#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y"), yo("yo"), yi("yi");

    f(x, y) = x + y;
    // A shear: the region of f required moves with x as well as y.
    g(x, y) = f(x, x + y) + f(x, x + y - 1);

    g.split(y, yo, yi, 4);
    // The window would have to advance within a single value of y, because x
    // moves it too.
    f.store_root().compute_at(g, x).slide(g, y);

    g.realize({8, 16});

    printf("Success!\n");
    return 0;
}
