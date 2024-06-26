#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "test_model.h"
#include <iostream>
#include <random>

int main(int argc, char **argv) {
    std::cout << "Running onnx_converter_generator_test...\n";
    Halide::Runtime::Buffer<float, 2> A(3, 4);
    Halide::Runtime::Buffer<float, 2> B(3, 4);
    Halide::Runtime::Buffer<float, 2> C(3, 4);

    std::mt19937 rnd(123);
    A.for_each_value([&](float &v) {
        v = rnd();
    });
    B.for_each_value([&](float &v) {
        v = rnd();
    });
    test_model(A, B, C);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (C(i, j) != A(i, j) + B(i, j)) {
                std::cerr << "Unexpected value for inputs at (" << i << "," << j << ") \n";
                return -1;
            }
        }
    }
    std::cout << "Success!\n";
    return 0;
}
