#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam im(UInt(8), 2);

    Var x, y;
    Func f;

    f(x, y) = im(x, y);

    Buffer<uint8_t> b(10, 10, 3);
    im.set(b);

    f.realize(10, 10);

    printf("There should have been an error\n");
    return 0;
}
