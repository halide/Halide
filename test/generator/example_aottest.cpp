#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "example.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int W = 110;
    const int H = 98;
    const int C = 3;

    Buffer<uint8_t, 3> input(W, H, C);
    // Contents don't really matter
    input.fill(0);

    Buffer<uint8_t, 3> output(W, H, C);

    // We can, of course, pass whatever values for Param/ImageParam that we like.
    example(input, output);

    return 0;
}
