#include <stdio.h>

#include "rdom_input.h"
#include "HalideBuffer.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    Buffer<uint8_t> input(3, 3);
    input.for_each_element([&](int x, int y) {
        input(x, y) = x * 16 + y;
    });

    Buffer<uint8_t> output(3, 3);
    rdom_input(input, output);

    output.for_each_element([&](int x, int y) {
        int expected = input(x, y) ^ 0xff;
        int actual = output(x, y);
        if (expected != actual) {
            fprintf(stderr, "output(%d, %d) was %d instead of %d\n", x, y, actual, expected);
            exit(1);
        }
    });

    printf("Success!\n");
    return 0;
}
