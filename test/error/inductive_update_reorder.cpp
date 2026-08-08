#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // reordering rvar outside of inductive dim should be illegal

    Func g("g"), h("h");
    Var x("x"), y("y");

    const int Y = 10;
    RDom ry(0, Y, "ry");

    g(x, y) = 0;
    g(x, y) = select(x <= 0, y, likely(max(g(x, y), g(x - 1, ry) + x + ry)));
    g.update().reorder(x, ry);

    h(x, y) = g(x, y);

    h.bound(x, 0, 20).bound(y, 0, Y);
    g.compute_at(h, x).store_root();

    h.realize({20, Y});

    printf("Success!\n");
    return 0;
}
