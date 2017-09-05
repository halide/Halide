#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    Func wrapper = f.in(g);
    wrapper(x) += 1;

    return 0;
}
