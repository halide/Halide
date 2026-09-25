#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("input_buffer_func_name_collision", "The name \"foo\" is used for both an input buffer (ImageParam or Generator Input<Buffer>) "
                                                          "and a Func in the same pipeline. Input buffers and Funcs must have distinct names.\n",
                      []() {
                          // Func declared before an ImageParam of the same name. Lowering
                          // should produce a clean user_error rather than crashing.
                          Var x;
                          Func existing("foo");
                          existing(x) = x;

                          ImageParam ip(Int(32), 1, "foo");

                          Func out("out");
                          out(x) = existing(x) + ip(x);

                          out.compile_jit();
                      });
}
