#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("specialize_fail", "A schedule specialized with specialize_fail() was chosen: Expected failure", []() {
        Var x;
        Param<int> p;

        Func f;
        f(x) = x;
        f.specialize(p == 0).vectorize(x, 8);
        f.specialize_fail("Expected failure");

        p.set(42);  // arbitrary nonzero value
        f.realize({100});
    });
}
