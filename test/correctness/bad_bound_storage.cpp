#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_bound_storage", "The explicit allocation bound (9) of dimension x of f is too small to store the required region (10).", []() {
        Func f("f"), g("g");
        Var x("x"), y("y");

        f(x, y) = x + y;
        g(x, y) = f(x, y) * 2;

        f.compute_at(g, y);
        f.bound_storage(x, 9);
        g.realize({10, 10});
    });
}
