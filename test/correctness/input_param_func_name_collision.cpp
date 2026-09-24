#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("input_param_func_name_collision", "The name \"foo\" is used for both a scalar Param (or Generator Input scalar) "
                                                         "and a Func in the same pipeline. Params and Funcs must have distinct names.\n",
                      []() {
                          // Func declared before a scalar Param of the same name. Lowering
                          // should produce a clean user_error rather than crashing.
                          Var x;
                          Func existing("foo");
                          existing(x) = x;

                          Param<int> p("foo");

                          Func out("out");
                          out(x) = existing(x) + p;

                          out.compile_jit();
                      });
}
