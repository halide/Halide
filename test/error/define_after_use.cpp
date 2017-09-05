#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x) + 1;

    // Now try to add an update definition to f
    f(x) += 1;

    printf("There should have been an error\n");
    return 0;
}
