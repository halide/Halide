#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("tuple_to_expr", "Can't treat this Tuple of size 2 as an Expr:\n(x, y, )\nOnly one-element Tuples can be cast to Expr.", []() {
        Var x("x"), y("y");

        // Only one-element Tuples can be used as Exprs.
        Expr e = Tuple(x, y);
        (void)e;
    });
}
