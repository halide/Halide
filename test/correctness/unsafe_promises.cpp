#include "Halide.h"

#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    // Test primary use case for unsafe_promise_clamped -- data that
    // uses fewer bits than the type it is embeded within.
    {
        Buffer<uint16_t> ten_bit_data(100);
        for (int i = 0; i < 100; i++) {
            ten_bit_data(i) = i * 10;
        }

        Buffer<float> ten_bit_lut(1024);

        for (int i = 0; i < 1024; i++) {
            ten_bit_lut(i) = sin(2 * 3.1415f * i / 1024.0f);
        }

        Var x;
        Func f;
        ImageParam in(UInt(16), 1);
        ImageParam lut(Float(32), 1);

        f(x) = lut(unsafe_promise_clamped(in(x), 0, 1023));
        lut.dim(0).set_bounds(0, 1024);

        in.set(ten_bit_data);
        lut.set(ten_bit_lut);

        auto result = f.realize(100);
    }

    {
        ImageParam in(UInt(8), 1);
        ImageParam lut(Float(32), 1);

        Var x;
        Func f;

        f(x) = lut(unsafe_promise_clamped(in(x), Expr(), 99));

        f.infer_input_bounds(10);
        Buffer<float> lut_bounds = lut.get();

        assert(lut_bounds.dim(0).min() == 0 && lut_bounds.dim(0).extent() == 100);
    }

    {
        ImageParam in(UInt(8), 1);
        ImageParam lut(Float(32), 1);

        Var x;
        Func f;

        f(x) = lut(unsafe_promise_clamped(in(x), 10, Expr()));

        f.infer_input_bounds(10);
        Buffer<float> lut_bounds = lut.get();

        assert(lut_bounds.dim(0).min() == 10 && lut_bounds.dim(0).extent() == 246);
    }

    std::cout << "Success!\n";
    return 0;
}
