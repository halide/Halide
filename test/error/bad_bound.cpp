#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f("f");
    Var x("x"), y("y");

    f(x) = 0;
    f.bound(y, 0, 10);

    return 0;
}
