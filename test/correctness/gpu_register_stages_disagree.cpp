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

    return error_test("gpu_register_stages_disagree", "The allocation f is scheduled to live in Register memory, which is private to a GPU thread, but it is scheduled outside the loops over GPU threads, so every thread gets its own copy of it rather than sharing one.", []() {
        Func f("f"), g("g");
        Var x("x"), y("y"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        f(x, y) += x + y;
        g(x, y) = f(x, y);

        g.gpu_tile(x, y, x, y, xi, yi, 16, 16);
        f.compute_at(g, x).store_in(MemoryType::Register).gpu_threads(x, y);

        // The update maps threads to sites transposed relative to the pure
        // definition, so it reads sites a different thread initialised.
        f.update().reorder(y, x).gpu_threads(y, x);

        g.compile_jit(target);
    });
}
