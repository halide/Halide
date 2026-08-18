#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Both f(x) and f(x-1) must be live to compute g, so a fold factor of 1 is unsafe.
int main(int argc, char **argv) {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));
    g(x, y) = f(x, y) + f(max(x - 1, 0), y);
    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.bound(x, 0, 64).bound(y, 0, 8);
    Buffer<int> im = g.realize({64, 8});

    printf("Success!\n");
    return 0;
}
