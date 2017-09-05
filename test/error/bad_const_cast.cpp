#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x;

    // The 256 here would be implicitly cast to uint8, and converted to
    // zero. That's bad. So we check for that inside IROperator.cpp.
    f(x) = cast<uint8_t>(x) % 256;

    printf("How did I get here?\n");
    return 0;
}
