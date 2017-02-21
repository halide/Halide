#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), z("z");

    Func f("f"), g("g"), h("h");

    g(x, y) = x + y;
    g.compute_root();

    h(x, y, z) = x + y + z;
    h.compute_root();

    // The initial definition uses 2 implicit vars: f(x, _0, _1) = g(_0, _1) + 2.
    // The update definition, however, calls h(_) which will be expanded into
    // h(_0, _1, _2), which is invalid.
    f(x, _) = g(_) + 2;
    f(x, _) += h(_) + 3;

    return 0;
}
