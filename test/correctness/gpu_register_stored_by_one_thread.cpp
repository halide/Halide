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

    return error_test("gpu_register_stored_by_one_thread", "The allocation f is scheduled to live in Register memory, which is private to a GPU thread, but it is scheduled outside the loops over GPU threads, so every thread gets its own copy of it rather than sharing one.", [&]() {
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

        g.compile_jit(target);
    });
}
