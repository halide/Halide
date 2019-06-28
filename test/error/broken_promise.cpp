#include <iostream>

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<uint16_t> ten_bit_data(100);
    for (int i = 0; i < 100; i++) {
        ten_bit_data(i) = i * 20;
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

    auto result = f.realize(100, get_jit_target_from_environment().with_feature(Target::CheckUnsafePromises));

    std::cout << "Success!\n";
    return 0;
}
