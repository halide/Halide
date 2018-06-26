#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <math.h>
#include <stdio.h>

#include "downsample.h"

using namespace Halide::Runtime;

const int kSize = 32;
const int kLogScale = 3;

int main(int argc, char **argv) {
    Buffer<uint8_t> input(kSize, kSize);
    Buffer<uint8_t> output(kSize >> kLogScale, kSize >> kLogScale);

    input.for_each_element([&](int x, int y) {
        input(x) = y * kSize + x;
    });

    downsample(kLogScale, input, output);

    const int scale = 1 << kLogScale;
    const int area = scale * scale;
    output.for_each_element([=](int x, int y) {
        uint16_t accumulator = 0;
        for (int dy = 0; dy < scale; ++dy) {
            for (int dx = 0; dx < scale; ++dx) {
                accumulator += input(scale * x + dx, scale * y + dy);
            }
        }
        assert(output(x, y) == accumulator / area);
    });

    printf("Success!\n");
    return 0;
}
