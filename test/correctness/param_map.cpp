#include "Halide.h"
#include <stdio.h>
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
            in2(i, j) = i * 10 + j;
        }
    }

    Target t = get_jit_target_from_environment();

    p_img.set(in1);
    Buffer<uint8_t> result1 = f.realize(10, 10, t);

    ParamMap params;
    params.set(p_int, 22);
    params.set(p_float, 2.0f);
    params.set(p_img, in2);

    Buffer<uint8_t> result2 = f.realize(10, 10, t, params);
    Buffer<uint8_t> result3 = f.realize(10, 10, t, { { p_int, 12} } );
    Buffer<uint8_t> result4 = f.realize(10, 10, t, { { p_int, 16}, {p_img, in2} } );

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            assert(result1(i, j) == i + j * 10 + 42);
            assert(result2(i, j) == i * 10 + j + 11);
            assert(result3(i, j) == i + j * 10 + 12);
            assert(result4(i, j) == i * 10 + j + 16);
        }
    }

    // Test bounds inference
    Buffer<uint8_t> in_bounds;

    f.infer_input_bounds({20, 20}, { { p_img, &in_bounds } });

    assert(in_bounds.defined());
    assert(in_bounds.dim(0).extent() == 20);
    assert(in_bounds.dim(1).extent() == 20);
    assert(in1.dim(0).extent() == 10);
    assert(in1.dim(1).extent() == 10);

    printf("Success!\n");
    return 0;
}
