#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    h(x, y) = g(x);

    g.compute_at(h, y);

    // This makes no sense, because the compute_at level is higher than the hoist_storage level
    f.hoist_storage(h, y).compute_root();

    h.realize({10, 10});

    printf("Success!\n");
    return 0;
}
