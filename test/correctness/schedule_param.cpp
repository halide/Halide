#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    ScheduleParam<LoopLevel> compute_at;
    ScheduleParam<int> vector_width, parallel_split;

    Var x("x"), y("y"), yi("yi");
    Func f("f"), g("g");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    f.compute_at(compute_at).vectorize(x, vector_width);
    g.split(y, y, yi, parallel_split).parallel(y);

    // We can set the ScheduleParam values any time before lowering.
    compute_at.set(LoopLevel(g, y));
    vector_width.set(4);
    parallel_split.set(2);

    constexpr int kEdge = 32;
    Buffer<int> out = g.realize(kEdge, kEdge, get_jit_target_from_environment());

    for (int i = 0; i < kEdge; i++) {
        for (int j = 0; j < kEdge; j++) {
            if (out(i, j) != i + j) {
                printf("Failed!\n");
                return -1;
            }
        }
    }

    // Setting them *after* the Funcs that use them have been jitted will have 
    // no effect, of course (unless you do something that invalidates the cached
    // jit code).
    // compute_at.set(LoopLevel(g, x));
    // vector_width.set(8);
    // parallel_split.set(4);

    printf("Success!\n");
    return 0;
}
