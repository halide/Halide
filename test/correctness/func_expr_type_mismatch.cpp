#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    return error_test("func_expr_type_mismatch", "Func \"f\" is constrained to only hold values of type float32 but is "
                                                 "defined with values of type int32.",
                      []() {
                          Var x("x"), y("y");
                          Func f(Float(32), 1, "f");

                          f(x, y) = cast<int>(0);

                          f.realize({100, 100});
                      });
}
