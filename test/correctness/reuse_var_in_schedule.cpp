#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("reuse_var_in_schedule", "can't create var v2 using a split or tile, because v2 is already used in this Func's schedule elsewhere.", []() {
        Func f;
        Var x;

        f(x) = x;

        Var xo, xi;
        f.split(x, xo, xi, 4).split(xo, xo, xi, 4);
    });
}
