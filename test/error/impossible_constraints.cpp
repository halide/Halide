#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    ImageParam input(Float(32), 2, "in");

    Func out("out");

    // The requires that the input be larger than the input
    out() = input(input.width(), input.height()) + input(0, 0);

    out.infer_input_bounds();

    return 0;
}

