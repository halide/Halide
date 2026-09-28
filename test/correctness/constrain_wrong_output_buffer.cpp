#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("constrain_wrong_output_buffer", "Can't constrain the min or extent of an output buffer beyond the first. They are implicitly constrained to have the same min and extent as the first output buffer.", []() {
        Func f;
        Var x;
        f(x) = Tuple(x, sin(x));

        // Don't do this. Instead constrain the size of output buffer 0.
        f.output_buffers()[1].dim(0).set_min(4);

        f.compile_jit();
    });
}
