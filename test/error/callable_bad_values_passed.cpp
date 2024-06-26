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

    Callable c = f.compile_to_callable({p_img, p_int, p_float});

    // Should fail with something like "Argument 2 of 4 ('p_int') was expected to be a scalar of type 'int32'."
    int r = c(in1, 3.1415927, 1.0f, result1);
    _halide_user_assert(r == 0);

    printf("Success!\n");
}
