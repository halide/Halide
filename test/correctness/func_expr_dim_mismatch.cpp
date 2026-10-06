#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    return error_test("func_expr_dim_mismatch", "Func \"f\" is constrained to have exactly 1 dimensions, but is defined "
                                                "with 2 dimensions.",
                      []() {
                          Var x("x"), y("y");
                          Func f(Int(32), 1, "f");

                          f(x, y) = cast<int>(0);

                          f.realize({100, 100});
                      });
}
