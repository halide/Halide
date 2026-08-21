#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        // An error test has to report an error even when it skips.
        _halide_user_assert(0);
    }

    Func g("g"), f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    g(x) = x;
    f(x, y) = g(x);

    f.gpu_tile(x, y, x, y, xi, yi, 16, 16);

    // g has no y, so only the threads at y == 0 store it, but every y reads
    // it. Each of the others would read its own copy, which it never wrote.
    g.compute_at(f, x).store_in(MemoryType::Register).gpu_threads(x);

    f.compile_jit(target);

    printf("Success!\n");
    return 0;
}
