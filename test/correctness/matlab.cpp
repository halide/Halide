#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(Float(32), 3, "input");
    Param<float> scale("scale");
    Param<bool> negate("negate");
    Var x, y, c;
    Func f("f");
    Expr scaled = input(x, y, c) * scale;
    scaled = select(negate, -scaled, scaled);
    f(x, y, c) = scaled;

    f.compile_to_matlab_object("f.o", {input, scale, negate});

    // This test only checks if we can successfully produce an
    // object. Testing the behavior of the result requires running
    // matlab.
    printf("Success!");
    return 0;
}
