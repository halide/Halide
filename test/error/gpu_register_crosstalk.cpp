#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = x + y * 1000;
    // The second term reads the value the neighbouring thread computed.
    g(x, y) = f(x, y) * 2 + f(x - 1, y - 1);

    g.gpu_tile(x, y, x, y, xi, yi, 16, 16);

    // f lives in registers, which are private to a thread, but it is computed
    // at the block level by all the threads together, so no thread has the
    // whole of it.
    f.compute_at(g, x).store_in(MemoryType::Register).gpu_threads(x, y);

    g.compile_jit(Target{"host-cuda"});

    printf("Success!\n");
    return 0;
}
