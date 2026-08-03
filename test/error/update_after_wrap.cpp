#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    // Wrapping f in g redirects g's calls to f eagerly, and freezes g.
    f.in(g);

    // Adding an update to g now would silently fail to be wrapped, so it is an
    // error.
    RDom r(0, 10);
    g(r, r) += 1;

    printf("Success!\n");
    return 0;
}
