#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("too_many_args", "was called with 2 arguments, but was defined with 1", []() {
        Var x, y;

        Func one_arg;
        one_arg(x) = x * 2;  // One argument

        Func bad_call;
        bad_call(x, y) = one_arg(x, y);  // Called with two

        // Should result in an error
        Buffer<uint32_t> result = bad_call.realize({256, 256});
        (void)result;
    });
}
