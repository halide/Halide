#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// rfactor() replaces a definition's RDom with the preserved RVars, named as
// the user named them. Specializations of one update stage may each split and
// rfactor the reduction differently, so an RVar of one name (here "ri") can
// have different bounds in different specializations. Each specialization
// must iterate over its own bounds, and the intermediates must be sized for
// whichever one runs.

namespace {

// dot(x, y) = sum_r a(r, x) * a(r, y), with `factors[i]` the split factor of
// specialization i (on extent(y) >= threshold[i]) and the last factor the
// default definition's.
int check(const std::vector<int> &factors, const std::vector<int> &thresholds) {
    const int K = 16, N = 8;
    Buffer<int> a(K, N);
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < K; x++) {
            a(x, y) = x + 3 * y + 1;
        }
    }

    Var x("x"), y("y"), u("u");
    RDom r(0, K, "r");
    Func dot("dot");
    dot(x, y) = 0;
    dot(x, y) += a(r, x) * a(r, y);

    Expr M = dot.output_buffer().dim(1).extent();
    Stage s = dot.update();
    for (size_t i = 0; i < factors.size(); i++) {
        Stage d = i < thresholds.size() ? s.specialize(M >= thresholds[i]) : s;
        RVar ro("ro"), ri("ri");
        d.split(r, ro, ri, factors[i], TailStrategy::GuardWithIf);
        Func intm = d.rfactor(ri, u);
        intm.compute_at(dot, y);
    }

    for (int m : {2, 4, 6, 8}) {
        Buffer<int> out = dot.realize({N, m});
        for (int j = 0; j < m; j++) {
            for (int i = 0; i < N; i++) {
                int correct = 0;
                for (int k = 0; k < K; k++) {
                    correct += a(k, i) * a(k, j);
                }
                if (out(i, j) != correct) {
                    printf("factors %d...: M = %d: out(%d, %d) = %d instead of %d\n",
                           factors[0], m, i, j, out(i, j), correct);
                    return 1;
                }
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    // A specialization's preserved RVar wider, then narrower, than the
    // default's, and two specializations that differ from each other.
    if (check({4, 2}, {8}) ||
        check({2, 4}, {8}) ||
        check({8, 4, 2}, {8, 4})) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
