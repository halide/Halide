#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // We cannot have a function be inductive in both a pure and update definition

    Func g(Int(32), "g"), h("h");
    Var x("x"), y("y");

    g(x, y) = select(x <= 0, y, likely(g(x - 1, y) + x));
    g(x, y) = select(y <= 0, g(x, y), likely(g(x, y - 1) + y));

    h(x, y) = g(x, y);

    h.bound(x, 0, 20).bound(y, 0, 20);
    g.compute_at(h, x).store_root();

    h.realize({20, 20});

    printf("Success!\n");
    return 0;
}
