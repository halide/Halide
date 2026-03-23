#include "Halide.h"
#include <iostream>
using namespace Halide;

// From https://github.com/halide/Halide/issues/7871

int main() {
    Func input("input");
    Func local_sum("local_sum");
    Func blurry("blurry");
    Var x("x"), y("y");
    RVar yryf;
    input(x, y) = 2 * x + 5 * y;
    RDom r(-2, 5, -2, 5, "rdom_r");
    local_sum(x, y) = 0;
    local_sum(x, y) += input(x + r.x, y + r.y);
    blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);

    // Should throw an error because we're trying to fuse a vectorized Var with
    // an impure RVar.
    local_sum.update(0).vectorize(y).fuse(y, r.y, yryf);

    printf("Success!\n");
    return 0;
}
