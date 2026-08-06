#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func g("g"), f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    g(x, y) = x + y;
    // One value of g per value of f, but not the one this thread stored.
    f(x, y) = g(2 * x, 2 * y);

    f.gpu_tile(x, y, x, y, xi, yi, 16, 16);
    g.compute_at(f, x).store_in(MemoryType::Register).gpu_threads(x, y);

    f.compile_jit(Target{"host-cuda"});

    printf("Success!\n");
    return 0;
}
