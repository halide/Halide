#include "Halide.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Internal;

// Regression test for solving `c <= min(a, b)` and its symmetric shapes in
// solve_for_{inner,outer}_interval. This vectorized LUT gather produces a
// clamped per-lane guard; count loop-dependent guards that should be removed
// from the steady state.

namespace {
int count_guards(Module m) {
    Scope<> loops;
    int guards = 0;
    for (const auto &f : m.functions()) {
        visit_with(
            f.body,
            [&](auto *self, const For *op) {
                ScopedBinding<> bind(loops, op->name);
                self->visit_base(op);
            },
            [&](auto *self, const IfThenElse *op) {
                if (expr_uses_vars(op->condition, loops)) {
                    guards++;
                }
                self->visit_base(op);
            });
    }
    return guards;
}
}  // namespace

int main(int argc, char **argv) {
    ImageParam input(UInt(8), 2, "input");
    Buffer<uint8_t> table(256, "table");

    Var x("x"), y("y"), xi("xi");

    Func gather("gather"), out("out");
    gather(x, y) = table(input(x, y));
    out(x, y) = gather(x, y);

    out.split(x, x, xi, 16, TailStrategy::GuardWithIf).vectorize(xi);
    gather.compute_at(out, x).unroll(x);

    Module m = out.compile_to_module({input}, "out", get_jit_target_from_environment());

    if (int guards = count_guards(m); guards != 0) {
        printf("Per-lane bounds guard was not partitioned out of the steady-state "
               "loop: found %d loop-dependent IfThenElse guard(s), expected 0\n",
               guards);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
