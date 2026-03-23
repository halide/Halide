#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    // Extern stage
    f.define_extern("make_data", {}, Int(32), 1);

    // Two-stage pipeline
    g(x, y) = f(2 * x + y);
    h(x) = g(x, 0) + g(x, 1);

    // Schedule with hoist_storage on an extern function
    // This should produce an error because buffer metadata needed
    // for device cleanup is only available at the store_at location
    h.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
    g.compute_at(h, x).unroll(y).vectorize(x);
    f.hoist_storage(g, Var::outermost()).bound_storage(_0, 16).compute_at(g, y);

    // This should fail with a user error
    h.compile_jit();

    printf("Success!\n");
    return 0;
}
