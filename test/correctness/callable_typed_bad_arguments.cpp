#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

void check(int r) {
    assert(r == 0);
}

int main(int argc, char **argv) {
    return error_test("callable_typed_bad_arguments", "Error defining 'f': Argument 1 of 4 ('p_int') was expected to be a scalar of type 'int32' and dimension 0.", []() {
        Param<int32_t> p_int(42);
        Param<float> p_float(1.0f);
        ImageParam p_img(UInt(8), 2);

        Var x("x"), y("y");
        Func f("f");

        f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

        Buffer<uint8_t> in1(10, 10), result1(10, 10);
        in1.fill(0);

        // Should fail with "Error defining 'f': Argument 1 of 4 ('p_int') was expected to be a scalar of type 'int32'."
        auto c = f.compile_to_callable({p_int, p_float, p_img})
                     .make_std_function<Buffer<uint8_t>, uint8_t, float, Buffer<uint8_t>>();
        (void)c;
    });
}
