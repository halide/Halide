#include <stdio.h>
#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
    Buffer<uint8_t> in1(256, 256);
    Buffer<uint8_t> in2(256, 256, 10);

    Func f;
    Var x, y;

    f(x, y) = in2(x, y, clamp(in1(x, y), 0, 9));
    Buffer<uint8_t> out = f.realize(256, 256);

    printf("Success!\n");
    return 0;
}
