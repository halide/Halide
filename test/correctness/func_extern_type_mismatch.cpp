#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    return error_test("func_extern_type_mismatch", "Func \"f\" is constrained to only hold values of type (uint8, float64) "
                                                   "but is defined with values of type (int32, float32).",
                      []() {
                          Var x("x"), y("y");
                          Func f({UInt(8), Float(64)}, 2, "f");
                          f.define_extern("test", {}, {Int(32), Float(32)}, {x, y});
                          f.realize({100, 100});
                      });
}
