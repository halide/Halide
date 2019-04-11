#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "test_model.h"
#include <iostream>
#include <random>

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<float> A(3, 4);
    Halide::Runtime::Buffer<float> B(3, 4);
    Halide::Runtime::Buffer<float> C(3, 4);

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
                std::cout << "Unexpcted value for inputs at (" << i << "," << j << ") " << std::endl;
                return -1;
            }
        }
    }
    std::cout << "Succssful!" << std::endl;
    return 0;
}
