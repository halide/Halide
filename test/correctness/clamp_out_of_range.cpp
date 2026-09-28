#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("clamp_out_of_range", "third argument (255) has type int32. Use an explicit cast.", []() {
        Var x;
        Func f;

        f(x) = clamp(cast<int8_t>(x), 0, 255);
    });
}
