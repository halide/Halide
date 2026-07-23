#include "Halide.h"
#include <cstdio>

// Stress test the sliding window pass on a pipeline with a long
// dependency chain of Funcs, each of which is computed in the
// same loop over the output.
// The partially compiled schedule has a long chain of let statements
// that represent the inferred bounds of each function. When
// analyzing the innermost functions during the
// sliding window pass, Halide used to expand the chain of let
// statements through direct substitution without CSE, which
// caused exponential blow-up. With CSE added, the blow-up is
// reduced to quadratic. The function call indices are chosen
// to make it difficult for the simplifier to eliminate the blow-up.
//
// We do not necessarily expect Halide to prove that the sliding window pass
// can be applied to every func in the chain, since the bounds
// expressions have large numbers of nodes even with CSE.

using namespace Halide;

int main(int argc, char **argv) {
    const int N = 100;
    Var x("x");
    Func f[N];
    Func out;
    f[0](x) = x % 2;
    f[0].store_root().compute_at(out, x);
    for (int i = 1; i < N; i++) {
        f[i](x) = f[i - 1](x - 1) + f[i - 1](min(x + 1, 20));
        f[i].store_root().compute_at(out, x);
    }
    out(x) = f[N - 1](x);
    out.bound(x, 0, 10);
    out.realize({10});
    out.compile_jit(get_jit_target_from_environment());
    printf("Success!\n");
    return 0;
}
