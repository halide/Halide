#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Func one_arg;
    one_arg(x) = x * 2;  // One argument

    Func bad_call;
    bad_call(x, y) = one_arg(x, y);  // Called with two

    // Should result in an error
    Buffer<uint32_t> result = bad_call.realize(256, 256);

    printf("Success!\n");
    return 0;
}
