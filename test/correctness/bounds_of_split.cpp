#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Verify that https://github.com/halide/Halide/issues/6186 is fixed
int main(int argc, char **argv) {
    Var x, y, chunk;

    ImageParam input(UInt(8), 2);

    Func intermed, output;
    intermed(x, y) = input(x, y);
    output(x, y) = intermed(x, y);

    // schedule
    intermed.compute_at(output, y);
    output.split(y, chunk, y, 64, TailStrategy::GuardWithIf);

    Buffer<uint8_t> input_buf(100, 100);
    input_buf.fill(0);
    input.set(input_buf);

    output.output_buffer().dim(0).set_min(0).set_extent(100);
    output.output_buffer().dim(1).set_min(0).set_extent(100);

    auto result = output.realize({100, 100});

    printf("Success!\n");

    return 0;
}
