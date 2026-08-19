#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Illegal to fold a dimension of a 2-d recurrence to a fold factor of 1,
// since it would create a read-after-write hazard.
int main(int argc, char **argv) {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x <= 0 || y <= 0, x + y, likely(f(x - 1, y - 1) + 1));
    g(x, y) = f(x, y);
    f.compute_at(g, x).store_root().fold_storage(y, 1);
    g.bound(x, 0, 64).bound(y, 0, 8);
    Buffer<int> im = g.realize({64, 8});

    printf("Success!\n");
    return 0;
}
