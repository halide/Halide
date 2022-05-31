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

    Buffer<uint8_t> in1(10, 10);
    Buffer<uint8_t> in2(10, 10);

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            in1(i, j) = i + j * 10;
            in2(i, j) = i * 10 + j;
        }
    }

    Target t = get_jit_target_from_environment();

    Buffer<uint8_t> result1, result2, result3, result4;

    Callable c = f.compile_to_callable({p_img, p_int, p_float}, t);

    result1 = Buffer<uint8_t>(10, 10);
    check(c(in1, 42, 1.0f, result1));

    result2 = Buffer<uint8_t>(10, 10);
    check(c(in2, 22, 2.0f, result2));

    result3 = Buffer<uint8_t>(10, 10);
    check(c(in1, 12, 1.0f, result3));

    result4 = Buffer<uint8_t>(10, 10);
    check(c(in2, 16, 1.0f, result4));

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            assert(result1(i, j) == i + j * 10 + 42);
            assert(result2(i, j) == i * 10 + j + 11);
            assert(result3(i, j) == i + j * 10 + 12);
            assert(result4(i, j) == i * 10 + j + 16);
        }
    }

    {
        // Test bounds inference
        Buffer<uint8_t> in_bounds(nullptr, 1, 1);
        Buffer<uint8_t> out_bounds(nullptr, 20, 20);

        check(c(in_bounds, 42, 1.0f, out_bounds));

        assert(in_bounds.defined());
        assert(in_bounds.dim(0).extent() == 20);
        assert(in_bounds.dim(1).extent() == 20);
        assert(in1.dim(0).extent() == 10);
        assert(in1.dim(1).extent() == 10);
    }

    //f.infer_input_bounds({20, 20}, t, {{p_img, &in_bounds}});

    printf("Success!\n");
}
