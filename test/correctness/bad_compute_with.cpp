#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_compute_with", "Cannot schedule f.update(0) to be computed with f.s0.x", []() {
        Func f("f");
        Var x("x"), y("y");

        f(x, y) = x + y;
        f(x, y) += 2;
        f.update(0).compute_with(f, x);

        f.realize({10, 10});
    });
}
