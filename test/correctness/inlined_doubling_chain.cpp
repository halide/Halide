#include "Halide.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Stress test for the BoundsInference Inliner's shared cached-body
// interaction with CSE's RemoveLets.
//
// Build a chain of inlined Funcs where each calls the previous one
// twice at different indices:
//
//     f_0(x)   = x
//     f_i(x)   = f_{i-1}(x) + f_{i-1}(x + 1)
//
// During lowering BoundsInference's Inliner inlines each f_i. It caches
// f_i's qualified body once and reuses the shared pointer at every call
// site, wrapping with `Let f_{i-1}.x = arg in <shared cached body>`. So
// in f_i's expanded body, two sibling Lets bind the same name
// `f_{i-1}.x` to *different* values (x and x + 1) around a single
// pointer-shared sub-tree representing f_{i-1}'s body. The DAG is O(N);
// its tree expansion (if you don't dedup shared sub-DAGs) is 2^N.
//
// `common_subexpression_elimination` then calls `RemoveLets()` first.
// RemoveLets is an IRGraphMutator, but its visit(Let) blanket-swaps
// expr_replacements on entry/exit -- it has to, because two sibling
// Lets that bind the same name to different values would otherwise
// cache-poison each other. The blanket swap means the shared body
// gets walked from scratch under each Let binding, materialising the
// 2^N tree expansion in memory.
//
// Pre-fix: even modest N (~25) crashes the compile from RAM exhaustion.
// Post-fix this should compile in well under a second at N=25, and
// scale comfortably to N=30+ for investigating the asymptotic.

using namespace Halide;

int main(int argc, char **argv) {
    // Default kept modest so the test passes quickly on CI even before
    // the bug is fixed. Override via argv[1] to investigate scaling.
    int N = 15;
    if (argc > 1) {
        N = std::atoi(argv[1]);
        if (N < 2 || N > 30) {
            fprintf(stderr, "N must be in [2, 30]\n");
            return 1;
        }
    }
    printf("N = %d\n", N);

    Var x("x");
    std::vector<Func> f(N);
    f[0] = Func("f_0");
    f[0](x) = x;
    for (int i = 1; i < N; i++) {
        f[i] = Func("f_" + std::to_string(i));
        f[i](x) = f[i - 1](x) + f[i - 1](x + 1);
    }
    Func out("out");
    out(x) = f[N - 1](x);

    auto t0 = std::chrono::steady_clock::now();
    Buffer<int> result = out.realize({8});
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    printf("Compile+realize took %.3fs\n", secs);

    // Closed form: f_i(x) = 2^i * x + i * 2^(i-1).
    auto expected = [&](int xi) {
        const int64_t coef = int64_t{1} << (N - 1);
        const int64_t shift = int64_t{N - 1} * (int64_t{1} << (N - 2));
        return (int)(coef * xi + shift);
    };
    for (int xi = 0; xi < 8; xi++) {
        int got = result(xi);
        int exp = expected(xi);
        if (got != exp) {
            fprintf(stderr, "Mismatch at x=%d: got %d, expected %d\n", xi, got, exp);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
