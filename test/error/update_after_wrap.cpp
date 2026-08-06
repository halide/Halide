#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    // Wrapping f in g redirects g's existing calls to f to the wrapper, and
    // freezes g.
    f.in(g);

    // This update calls f, but the eager rewrite already happened, so it would
    // call f directly rather than the wrapper -- inconsistent with g's original
    // definition. Adding updates to a wrapped consumer is therefore an error.
    RDom r(0, 10);
    g(r, r) += f(r, r);

    printf("Success!\n");
    return 0;
}
