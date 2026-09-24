#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    return error_test("gpu_register_stored_by_one_warp", "The allocation g is scheduled to live in Register memory, which is private to a GPU thread, but it is scheduled outside the loops over GPU threads, so every thread gets its own copy of it rather than sharing one.", []() {
        Func g("g"), f("f");
        Var x("x"), y("y"), xi("xi"), yi("yi");

        g(x) = x;
        f(x, y) = g(x);

        f.gpu_tile(x, y, x, y, xi, yi, 16, 16);

        // g has no y, so only the threads at y == 0 store it, but every y reads
        // it. Each of the others would read its own copy, which it never wrote.
        g.compute_at(f, x).store_in(MemoryType::Register).gpu_threads(x);

        f.compile_jit(target);
    });
}
