#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    Func f;

    f(x) = clamp(cast<int8_t>(x), 0, 255);
    Buffer<> result = f.realize(42);

    printf("Success!\n");

    printf("I should not have reached here\n");
    return 0;
}
