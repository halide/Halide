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

    // Should fail with "Generated code refers to parameter p_int, which was not found in the argument list."
    Callable c = f.compile_to_callable({p_img, p_float});

    // Shouldn't get here, but if we do, return success, which is a failure...

    printf("Success!\n");
    return 0;
}
