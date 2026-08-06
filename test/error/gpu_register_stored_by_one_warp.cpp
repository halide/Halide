#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func g("g"), f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    g(x) = x;
    f(x, y) = g(x);

    f.gpu_tile(x, y, x, y, xi, yi, 16, 16);

    // g has no y, so only the threads at y == 0 store it, but every y reads
    // it. Each of the others would read its own copy, which it never wrote.
    g.compute_at(f, x).store_in(MemoryType::Register).gpu_threads(x);

    f.compile_jit(Target{"host-cuda"});

    printf("Success!\n");
    return 0;
}
