#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
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
