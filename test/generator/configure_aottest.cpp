#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "configure.h"

using Halide::Runtime::Buffer;

const int kSize = 32;

int main(int argc, char **argv) {

    Buffer<int, 3> input(kSize, kSize, 3);
    input.for_each_element([&](int x, int y, int c) {
        input(x, y, c) = (x * 3 + y * 5 + c * 7);
    });

    std::vector<Buffer<uint8_t, 2>> extras;
    int extra_value = 0;
    for (int i = 0; i < 3; ++i) {
        extras.push_back(Buffer<uint8_t, 2>(kSize, kSize));
        extras.back().fill((uint8_t)i);
        extra_value += i;
    }

    Buffer<int16_t, 2> typed_extra(kSize, kSize);
    typed_extra.fill(4);
    extra_value += 4;

    // Funcs are aot-compiled as buffers.
    Buffer<uint16_t, 3> func_extra(kSize, kSize, 3);
    func_extra.fill(5);
    extra_value += 5;

    const int extra_scalar = 7;
    const int8_t extra_dynamic_scalar = 13;
    extra_value += extra_scalar + extra_dynamic_scalar;

    Buffer<int, 3> output(kSize, kSize, 3);
    Buffer<float, 3> extra_buffer_output(kSize, kSize, 3);
    Buffer<double, 2> extra_func_output(kSize, kSize);

    const int bias = 1;
    int result = configure(input, bias,
                           // extra inputs are in the order they were added, after all predeclared inputs
                           extras[0], extras[1], extras[2], typed_extra, func_extra, extra_scalar, extra_dynamic_scalar,
                           output,
                           // extra outputs are in the order they were added, after all predeclared outputs
                           extra_buffer_output, extra_func_output);
    if (result != 0) {
        fprintf(stderr, "Result: %d\n", result);
        exit(-1);
    }

    output.for_each_element([&](int x, int y, int c) {
        assert(output(x, y, c) == input(x, y, c) + bias + extra_value);
    });

    extra_buffer_output.for_each_element([&](int x, int y, int c) {
        assert(extra_buffer_output(x, y, c) == output(x, y, c));
    });

    extra_func_output.for_each_element([&](int x, int y) {
        assert(extra_func_output(x, y) == output(x, y, 0));
    });

    printf("Success!\n");
    return 0;
}
