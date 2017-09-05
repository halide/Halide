#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x;
    f(x) = x;
    Buffer<float> im = f.realize(100);

    return 0;
}
