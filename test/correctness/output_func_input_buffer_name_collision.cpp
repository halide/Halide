#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("output_func_input_buffer_name_collision", "The name \"foo\" is used for both an input buffer (ImageParam or Generator Input<Buffer>) and a Func in the same pipeline. Input buffers and Funcs must have distinct names.", []() {
        // The pipeline's own output Func is declared before an ImageParam of
        // the same name, and the ImageParam is used as an input to that same
        // Func. Because the Func is the pipeline output, its own auto-created
        // output buffer Parameter also shares this name -- but the ImageParam
        // is a distinct Parameter object, so this must still be flagged as a
        // collision rather than silently aliasing the two buffers.
        Var x;
        Func foo("foo");
        ImageParam foo_param(Int(32), 1, "foo");
        foo(x) = foo_param(x) + 1;

        foo.compile_jit();
    });
}
