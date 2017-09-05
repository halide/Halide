#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Var x("x");
    Func f("f");

    // Should result in an error
    f(x) = {x, select(x < 20, 20*x, undef<int>())};
    f.realize(10);

    printf("Success!\n");
    return 0;
}
