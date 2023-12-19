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

    // ring_buffer() requires an explicit hoist_storage().
    f.compute_root().ring_buffer(2);

    h.realize({10, 10});

    printf("Success!\n");
    return 0;
}
