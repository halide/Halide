#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x;
    f(x) = Expr(0x12345678) * Expr(0x76543210);

    f.realize(10);

    return 0;
}
