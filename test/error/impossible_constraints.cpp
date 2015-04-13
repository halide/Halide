#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(Float(32), 2, "in");

    Func out("out");

    // The requires that the input be larger than the input
    out() = input(input.width(), input.height()) + input(0, 0);

    out.infer_input_bounds();

    return 0;
}

