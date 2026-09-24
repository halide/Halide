#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

void check(int r) {
    assert(r == 0);
}

int main(int argc, char **argv) {
    return error_test("callable_bad_arguments", "Generated code refers to parameter p_int, which was not found in the argument list.", []() {
        Param<int32_t> p_int(42);
        Param<float> p_float(1.0f);
        ImageParam p_img(UInt(8), 2);

        Var x("x"), y("y");
        Func f("f");

        f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

        // Should fail with "Generated code refers to parameter p_int, which was not found in the argument list."
        Callable c = f.compile_to_callable({p_img, p_float});
        (void)c;
    });
}
