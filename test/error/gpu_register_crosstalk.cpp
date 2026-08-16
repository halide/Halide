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

    g.compile_jit(target);

    printf("Success!\n");
    return 0;
}
