#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    return error_test("func_tuple_update_types_mismatch", "In update definition 0 of Func \"f\":\n"
                                                          "Tuple element 0 of update definition has type int32, but pure definition "
                                                          "has type uint8",
                      []() {
                          Var x("x"), y("y");
                          Func f({UInt(8), Float(64)}, 2, "f");

                          f(x, y) = {cast<uint8_t>(0), cast<double>(0)};
                          f(x, y) = {cast<int>(0), cast<float>(0)};

                          f.realize({100, 100});
                      });
}
