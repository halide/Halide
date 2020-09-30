#include <stdio.h>

#include "HalideBuffer.h"
#include "shuffler.h"

using Halide::Runtime::Buffer;

constexpr int W = 256;

int main(int argc, char **argv) {
    Buffer<int32_t> input(W);
    for (int x = 0; x < W; x++) {
        input(x) = x;
    }

    Buffer<int32_t> output(W / 4);
    shuffler(input, output);

    for (int x = 0; x < W / 4; x++) {
        int expected = input(input(x / 2 + 1) / 2 + 1);
        int actual = output(x);
        if (expected != actual) {
            printf("at x = %d expected %d got %d\n", x, expected, actual);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
