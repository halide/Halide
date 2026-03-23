#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;

void check(int r) {
    assert(r == 0);
}

int main(int argc, char **argv) {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Buffer<uint8_t> in1(10, 10), result1(10, 10);
    in1.fill(0);

    // Should fail with "Error defining 'f': Argument 1 of 4 ('p_img') was expected to be a buffer of type 'uint8' and dimension 2."
    auto c = f.compile_to_callable({p_img, p_int, p_float})
                 .make_std_function<Buffer<uint8_t, 3>, int32_t, float, Buffer<uint8_t, 2>>();

    // Shouldn't get here, but if we do, return success, which is a failure...

    printf("Success!\n");
}
