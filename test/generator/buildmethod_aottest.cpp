#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "buildmethod.h"

using namespace Halide::Runtime;

const int kSize = 32;

int main(int argc, char **argv) {
    Buffer<float, 3> input(kSize, kSize, 3);
    Buffer<int32_t, 3> output(kSize, kSize, 3);

    const float compiletime_factor = 1.0f;
    const float runtime_factor = 3.25f;

    input.for_each_element([&](int x, int y, int c) {
        input(x, y, c) = std::max(x, y) * c;
    });

    buildmethod(input, runtime_factor, output);

    output.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * std::max(x, y));
        int actual = output(x, y, c);
        assert(expected == actual);
    });

    printf("Success!\n");
    return 0;
}
