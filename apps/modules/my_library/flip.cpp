#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func flip;
    Var x, y;

    ImageParam in(Float(32), 2);
    Param<int> total_width;

    flip(x, y) = in(total_width - 1 - x, y);

    flip.vectorize(x, 4);

    flip.compile_to_file("flip_impl", in, total_width);

    return 0;
}

