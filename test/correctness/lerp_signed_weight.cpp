#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("lerp_signed_weight", "A lerp weight must be an unsigned integer or a float, but lerp weight 16_i8 has type int8.", []() {
        // This should trigger an error.
        Func f;
        f() = lerp(cast<uint8_t>(0), cast<uint8_t>(42), cast<int8_t>(16));
    });
}
