#include "Halide.h"

#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x, y;
    f(x, y) = 3*x + y;

    // Should result in an error
    Func g;
    g(x) = f(f(x, 3) * 17.0f, 3);

    printf("Success!\n");
    return 0;
}

