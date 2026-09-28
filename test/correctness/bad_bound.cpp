#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_bound", "Can't bound variable y of function f because y is not one of the pure variables of f.", []() {
        Func f("f");
        Var x("x"), y("y");

        f(x) = 0;
        f.bound(y, 0, 10);
    });
}
