#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void check(Func f, ImageParam in, int min, int extent) {
    Buffer<int> output(12345);
    output.set_min(-1234);

    in.reset();
    f.infer_input_bounds(output);
    Buffer<int> im = in.get();

    if (im.extent(0) != extent || im.min(0) != min) {
        printf("Inferred size was [%d, %d] instead of [%d, %d]\n",
               im.min(0), im.extent(0), min, extent);
        exit(-1);
    }
}

int main(int argc, char **argv) {
    ImageParam input(Int(32), 1);
    Var x;
    Func f1 = lambda(x, input(cast<int8_t>(x) << 2));
    Func f2 = lambda(x, input(cast<int8_t>(x) >> 2));
    Func f3 = lambda(x, input(cast<uint8_t>(x) << 3));
    Func f4 = lambda(x, input(cast<uint8_t>(x) >> 3));
    Func f5 = lambda(x, input(cast<int32_t>(x) >> 1));

    // input should be the normal range for an int8.
    check(f1, input, -128, 256);
    // input should be a quarter of the range of an int8.
    check(f2, input, -32, 64);
    // input should be the normal range for a uint8.
    check(f3, input, 0, 256);
    // input should be 1/8th the normal range for a uint8.
    check(f4, input, 0, 32);
    // input should be 1/2 the actual buffer size.
    check(f5, input, -617, 6173);

    printf("Success!\n");
    return 0;
}
