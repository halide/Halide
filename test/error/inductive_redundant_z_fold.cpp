#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Redundantly recomputes f(x, y) for every z, so f(x - 1, y)
// cannot safely be overwritten when computing f(x, y).
// A fold factor of 1 for x is unsafe.
int main(int argc, char **argv) {
    Func f(Int(32), "f"), h("h");
    Var x("x"), y("y"), z("z");
    f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));
    h(x, y, z) = f(x, y);
    h.reorder(z, y, x);  // x outer, then y, z inner
    f.compute_at(h, z).store_root().fold_storage(x, 1);
    h.bound(x, 0, 64).bound(y, 0, 8).bound(z, 0, 4);
    Buffer<int> im = h.realize({64, 8, 4});

    printf("Success!\n");
    return 0;
}
