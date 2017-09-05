#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Param<const char *> p;
    p.set("Hello, world!\n");

    Func f;
    Var x;
    // Should error out during match_types
    f(x) = p + 2;

    return 0;
}
