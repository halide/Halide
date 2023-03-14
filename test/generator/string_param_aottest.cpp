#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "string_param.h"
#include <iostream>

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<int, 2> output(3, 3);
    string_param(output);

    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            int expected_value = (5 * y + x);
            if (output(x, y) != expected_value) {
                printf("Unexpected output value : %d at output(%d, %d)\n", output(x, y), x, y);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
