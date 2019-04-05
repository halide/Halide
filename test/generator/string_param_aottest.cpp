#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "string_param.h"
#include <iostream>

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<float> x(3);
    x(0) = 1.0f;
    x(1) = 2.0f;
    x(2) = 3.0f;

    Halide::Runtime::Buffer<float> y(3);
    string_param(x, y);

    for (int i = 0; i < 3; ++i) {
        if (y(i) != x(i) + 1) {
            printf("Unexpected output value : %f at y(%d)\n", y(i), i);
        }
    }

    printf("Success!\n");
    return 0;
}
