#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x;
    // You can't use the same variable more than once in the LHS of a
    // pure definition.
    f(x, x) = x;

    return 0;
}
