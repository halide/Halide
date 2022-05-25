#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "alias.h"
#include "alias_with_offset_42.h"

using namespace Halide::Runtime;

const int kSize = 32;

int main(int argc, char **argv) {
    Buffer<int32_t, 1> input(kSize), output(kSize);

    input.for_each_element([&](int x) {
        input(x) = x;
    });

    alias(input, output);
    input.for_each_element([=](int x) {
        assert(output(x) == input(x));
    });

    alias_with_offset_42(input, output);
    input.for_each_element([=](int x) {
        assert(output(x) == input(x) + 42);
    });

    printf("Success!\n");
    return 0;
}
