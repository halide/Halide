#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("lerp_float_weight_out_of_range", "Floating-point weight for lerp with integer arguments is 1.5, which is not in the range [0.0, 1.0].\n", []() {
        // This should trigger an error.
        Func f;
        f() = lerp(0, 42, 1.5f);
    });
}
