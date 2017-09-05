#include "Halide.h"

#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x;
    f(x) = x % 0;

    f.realize(10);

    printf("Success!\n");
    return 0;
}

