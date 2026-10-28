#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("compute_at_on_output", "Func g is an output, so must be scheduled compute_root, store_root, and hoist_storage_root", []() {
        Func f("f"), g("g");
        Var x("x");

        f(x) = x;
        g(x) = x + 1;

        // g is an output, so it can't be scheduled compute_at another Func, even if
        // that func is realized first and is also an output.
        g.compute_at(f, x);

        Pipeline({f, g}).compile_jit();
    });
}
