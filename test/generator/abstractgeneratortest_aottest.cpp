#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "abstractgeneratortest.h"

using namespace Halide::Runtime;

const int kSize = 4;

void verify(const Buffer<int32_t> &img, float compiletime_factor, float runtime_factor, int channels) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y));
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

int main(int argc, char **argv) {

    const int32_t scaling = 2;  // GeneratorParam

    Buffer<int32_t> input(kSize, kSize);
    const int32_t offset = 32;

    input.for_each_element([&](int x, int y) {
        input(x, y) = (x + y);
    });

    Buffer<int32_t> output(kSize, kSize);
    abstractgeneratortest(input, offset, output);

    output.for_each_element([&](int x, int y) {
        int expected = (x + y) * scaling + offset;
        int actual = output(x, y);
        if (expected != actual) {
            fprintf(stderr, "at %d %d, expected %d, actual %d\n", x, y, expected, actual);
            exit(1);
        }
    });

    printf("Success!\n");
    return 0;
}
