#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "alias.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
  constexpr int kEdge = 256;
    Buffer<int32_t> input(kEdge, kEdge), output(kEdge, kEdge);

    input.for_each_element([&](int x, int y) {
        input(x, y) = x + y;
    });

    alias(input, output);

    printf("Success!\n");
    return 0;
}
