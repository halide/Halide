#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "bit_operations.h"

using namespace Halide::Runtime;

const int kSize = 1024;

template<typename T>
uint8_t _count_leading_zeros(T v) {
    int bits = sizeof(v) * 8;
    while (v) {
        v >>= 1;
        bits--;
    }
    return bits;
}

int main(int argc, char **argv) {
    Buffer<uint8_t> input8(kSize);
    Buffer<uint16_t> input16(kSize);
    Buffer<uint32_t> input32(kSize);
    Buffer<uint64_t> input64(kSize);
    Buffer<uint8_t> output8(kSize);
    Buffer<uint8_t> output16(kSize);
    Buffer<uint8_t> output32(kSize);
    Buffer<uint8_t> output64(kSize);

    for (int i = 0; i < kSize; i++) {
        input8(i) = i;
        input16(i) = i * i;
        input32(i) = i * i * i;
        input64(i) = i * i * i * i;
    }

    bit_operations(input8, input16, input32, input64, output8, output16, output32, output64);

    for (int i = 0; i < kSize; i++) {
        assert(output8(i) == _count_leading_zeros(input8(i)));
        assert(output16(i) == _count_leading_zeros(input16(i)));
        assert(output32(i) == _count_leading_zeros(input32(i)));
        assert(output64(i) == _count_leading_zeros(input64(i)));
    }

    return 0;
}
