#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f;
    Var x, y;

    ImageParam in(Float(32), 2);
    ImageParam x_coord(Int(32), 2);
    ImageParam y_coord(Int(32), 2);

    f(x, y) = 0.0f;
    RDom r(0, 100, 0, 100);
    f(x_coord(r.x, r.y), y_coord(r.x, r.y)) += in(r.x, r.y);

    f.compile_jit();

    printf("I should not have reached here\n");

    return 0;
}
