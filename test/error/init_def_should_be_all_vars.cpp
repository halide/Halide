#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Buffer<int> in(10, 10);

    Func f("f");
    RDom r(0, in.width(), 0, in.height());
    f(r.x, r.y) = in(r.x, r.y) + 2;
    f.realize(in.width(), in.height());

    return 0;
}
