#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Target t = get_jit_target_from_environment();
    t.set_feature(Target::CUDA);

    Func f;
    Var x;
    f(x) = x;
    Var xo, xi;
    f.gpu_tile(x, xo, xi, 16).reorder(xo, xi);

    f.compile_jit(t);
    Buffer<int> result = f.realize(16);

    printf("There should have been an error\n");
    return 0;
}

