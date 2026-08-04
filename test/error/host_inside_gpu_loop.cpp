#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Func g("g");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    g.compute_root().gpu_tile(x, xo, xi, 256);
    f.compute_at(g, xo).host();

    g.compile_jit(Target{"host-opencl"});

    printf("Success!\n");
    return 0;
}
