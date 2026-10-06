#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_fold", "fold factor (2) of dimension v1 of f0 is too small to store the required region", []() {
        Var x, y, c;

        Func f, g;

        f(x, y) = x;
        g(x, y) = f(x - 1, y + 1) + f(x, y - 1);
        f.store_root().compute_at(g, y).fold_storage(y, 2);

        Buffer<int> im = g.realize({100, 1000});
    });
}
