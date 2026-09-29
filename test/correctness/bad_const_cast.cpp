#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_const_cast", "Integer constant 256 will be implicitly coerced to type uint8, which changes its value to 0_u8.", []() {
        Func f;
        Var x;

        // The 256 here would be implicitly cast to uint8, and converted to
        // zero. That's bad. So we check for that inside IROperator.cpp.
        f(x) = cast<uint8_t>(x) % 256;
    });
}
