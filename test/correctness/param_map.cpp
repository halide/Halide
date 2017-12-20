#include <stdio.h>
#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Buffer<uint8_t> in1(10, 10);
    Buffer<uint8_t> in2(10, 10);

    for (int i = 0; i < 10; i++) {
      for (int j = 0; j < 10; j++) {
            in1(i, j) = i + j * 10;
            in2(j, i) = in1(i, j);
        }
    }

    Target t = get_jit_target_from_environment();

    p_img.set(in1);
    Buffer<uint8_t> result1 = f.realize(10, 10, t);

    ParamMap params;
    params.set(p_int, 22);
    params.set(p_float, 2.0f);
    Buffer<> temp = in2;
    params.set(p_img, temp);

    Buffer<uint8_t> result2 = f.realize(10, 10, t, params);

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            assert(result1(i, j) = i + j * 10 + 42);
            assert(result2(j, i) = i * 10 + j + 11);
        }
    }

    printf("Success!\n");
    return 0;
}
