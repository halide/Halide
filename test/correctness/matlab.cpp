#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(Float(32), 3, "input");
    Var x, y, c;
    Func f("f");
    f(x, y, c) = input(x, y, c) * 2.0f;

    f.compile_to_matlab_object("f.o", {input});

    // This test only checks if we can successfully produce an
    // object. Testing the behavior of the result requires running
    // matlab.
    printf("Success!");
    return 0;
}
