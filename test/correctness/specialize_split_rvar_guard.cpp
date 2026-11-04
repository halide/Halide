#include "Halide.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Internal;

// An RVar can only be split with GuardWithIf, so splitting one of symbolic
// extent K leaves a guard against K in the loop over the outer RVar, even
// where a specialization says K is a multiple of the factor. The simplifier
// must prove the guard from the loop's bounds (and K's alignment) so that the
// specialized loop is unguarded. Partitioning the loop would otherwise hide
// the guard in an epilogue, and isn't always possible (e.g. GPU loops
// containing barriers), so turn it off.

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
    ImageParam in(Float(32), 1, "in");
    Expr K = in.dim(0).extent();
    RDom r(0, K);

    Func f("f");
    f() = 0.f;
    f() += in(r);

    // Steps of 64 values in pieces of 8, as for a matrix multiply's tiles.
    RVar ks("ks"), ko("ko"), kt("kt");
    Stage s = f.update();
    s.split(r, ks, ko, 64)
        .split(ko, ko, kt, 8)
        .unroll(ko)
        .atomic()
        .vectorize(kt)
        .partition(ks, Partition::Never);
    s.specialize(K % 64 == 0);
    s.specialize_fail("K must be a multiple of 64");

    Module m = f.compile_to_module({in}, "f", get_jit_target_from_environment());
    if (int guards = count_guards(m); guards != 0) {
        printf("The guards of a split RVar weren't proven from a specialization "
               "on its extent: found %d loop-dependent IfThenElse guard(s), "
               "expected 0\n",
               guards);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
