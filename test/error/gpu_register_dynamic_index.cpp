#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi"), xii("xii"), yii("yii");
    Var fxo("fxo"), fyo("fyo"), fxi("fxi"), fyi("fyi");

    f(x, y) = x + y * 1000;
    g(x, y) = f(x, y) * 2;

    g.split(x, xo, xi, 32)
        .split(y, yo, yi, 32)
        .split(xi, xi, xii, 2)
        .split(yi, yi, yii, 2)
        .reorder(xii, yii, xi, yi, xo, yo)
        .gpu_blocks(xo, yo)
        .gpu_threads(xi, yi);

    // Each thread walks its own 2x2 tile with serial loops rather than
    // unrolled ones, so which register to use is only known while running.
    // Registers cannot be indexed dynamically.
    f.compute_at(g, xo)
        .store_in(MemoryType::Register)
        .split(x, fxo, fxi, 2)
        .split(y, fyo, fyi, 2)
        .reorder(fxi, fyi, fxo, fyo)
        .gpu_threads(fxo, fyo);

    g.compile_jit(target);

    printf("Success!\n");
    return 0;
}
