#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // The check happens when the pipeline is compiled, so it needs a GPU API
    // but not a GPU. Use the one the environment names, and pick one if it
    // names none, so that this is still tested on a machine without a GPU.
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        target.set_feature(Target::CUDA);
    }

    Func g("g"), f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    g(x, y) = x + y;
    // One value of g per value of f, but not the one this thread stored.
    f(x, y) = g(2 * x, 2 * y);

    f.gpu_tile(x, y, x, y, xi, yi, 16, 16);
    g.compute_at(f, x).store_in(MemoryType::Register).gpu_threads(x, y);

    f.compile_jit(target);

    printf("Success!\n");
    return 0;
}
