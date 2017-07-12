#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // What happens if an emedded image gets simplified away?
    Buffer<float> input(32, 32);

    Var x("x"), y("y");
    Func foo("foo");

    foo(x, y) = input(x, y) - input(x, y);

    Buffer<float> output(32, 32);

    foo.realize(output);

    // Any non-error is a success.
    printf("Success!\n");

    return 0;
}
