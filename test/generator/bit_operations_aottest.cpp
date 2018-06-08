#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <math.h>
#include <stdio.h>

#include "bit_operations.h"

using namespace Halide::Runtime;

const int kSize = 1024;

uint8_t _count_leading_zeros(uint64_t v) {
    int bits = sizeof(v) * 8;
    while (v) {
        v >>= 1;
        bits--;
    }
    return bits;
}

int main(int argc, char **argv) {
    Buffer<uint64_t> input(kSize);
    Buffer<uint8_t> output(kSize);

    input.for_each_element([&](int x) {
        input(x) = x * x * x;
    });

    bit_operations(input, output);
    input.for_each_element([=](int x) {
        assert(output(x) == _count_leading_zeros(input(x)));
    });

    return 0;
}
