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
        exit(1);
    }
}

int main(int argc, char **argv) {
    ImageParam input(Int(32), 1);
    Var x;
    Func f1 = lambda(x, input(cast<uint8_t>(x)));
    Func f2 = lambda(x, input(cast<int8_t>(x)));
    Func f3 = lambda(x, input(cast<uint16_t>(x)));
    Func f4 = lambda(x, input(cast<int16_t>(x)));

    // input should only be required from 0 to 256
    check(f1, input, 0, 256);
    check(f2, input, -128, 256);
    check(f3, input, 0, 65536);
    check(f4, input, -32768, 65536);

    printf("Success!\n");
    return 0;
}
