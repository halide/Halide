#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Var x;
    Param<int> p;

    Func f;
    f(x) = x;
    f.specialize(p == 0).vectorize(x, 8);
    f.specialize_fail("Expected failure");

    p.set(42);  // arbitrary nonzero value
    f.realize(100);

    printf("How did I get here?\n");

    return 0;
}
