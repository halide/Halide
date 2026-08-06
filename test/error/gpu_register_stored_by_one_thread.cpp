#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f() = 42;
    g(x, y) = f() + x;

    g.gpu_tile(x, y, x, y, xi, yi, 16, 16);

    // f is computed at the block level with no loops over threads of its own,
    // so fusing the thread loops leaves its store guarded by a test that only
    // the first thread passes. Every other thread would read its own copy of
    // an allocation only the first thread wrote.
    f.compute_at(g, x).store_in(MemoryType::Register);

    g.compile_jit(Target{"host-cuda"});

    printf("Success!\n");
    return 0;
}
