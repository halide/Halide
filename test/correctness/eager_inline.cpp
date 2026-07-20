#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

// eager_inline() inlines direct calls at schedule time (unlike compute_inline(),
// which defers to lowering), which lets later schedule-time directives see the
// exposed structure. Here an invariant factor S(x) is buried inside a call
// f(x, r); inlining f (and the g it calls) exposes it so hoist_invariants() can
// lift it out of the reduction.
int eager_inline_exposes_factor_test() {
    const int K = 16, M = 4;
    ImageParam S{Float(32), 1, "S"};
    ImageParam data{Float(32), 2, "data"};

    Var x{"x"}, y{"y"};
    RDom r(0, K, "r");

    Func g{"g"}, f{"f"}, h{"h"};
    g(x) = S(x);
    f(x, y) = g(x) * data(x, y);
    h(x) += f(x, r);

    // Inline f then g into h, left to right: inlining f exposes the call to g.
    h.eager_inline({f, g});
    // Now h(x) += S(x) * data(x, r), so the invariant S(x) can be hoisted.
    Func h_wb = h.update().hoist_invariants();
    h_wb.compute_root();

    Buffer<float> sbuf(M), dbuf(M, K);
    for (int i = 0; i < M; i++) {
        sbuf(i) = 0.5f * (i + 1);
        for (int k = 0; k < K; k++) {
            dbuf(i, k) = (float)((i + k) % 5 - 2);
        }
    }
    S.set(sbuf);
    data.set(dbuf);

    Buffer<float> out = h.realize({M});
    for (int i = 0; i < M; i++) {
        float ref = 0.f;
        for (int k = 0; k < K; k++) {
            ref += sbuf(i) * dbuf(i, k);
        }
        if (std::abs(out(i) - ref) > 1e-4f) {
            printf("eager_inline exposes factor mismatch at %d: %f vs %f\n", i, out(i), ref);
            return 1;
        }
    }
    return 0;
}

// eager_inline() performs the substitution immediately, so the caller's
// definition no longer references the inlined Funcs (they are inlined by value).
// Verify the numerics of a simple chained inline match a plain inlined pipeline.
int eager_inline_chain_test() {
    Var x{"x"};
    Func a{"a"}, b{"b"}, c{"c"};
    a(x) = x + 1;
    b(x) = a(x) * 2;     // calls a
    c(x) = b(x) + a(x);  // calls b (which calls a) and a directly

    // Inline b then a into c. Inlining b splices in its call to a, which the
    // subsequent inline of a then also folds.
    c.eager_inline({b, a});

    Buffer<int> out = c.realize({8});
    for (int i = 0; i < 8; i++) {
        int ref = (i + 1) * 2 + (i + 1);
        if (out(i) != ref) {
            printf("eager_inline chain mismatch at %d: %d vs %d\n", i, out(i), ref);
            return 1;
        }
    }
    return 0;
}

#if HALIDE_WITH_EXCEPTIONS
// A Func with an update definition is not inlinable, so eager_inline() rejects it.
int eager_inline_non_pure_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    Var x{"x"};
    RDom r(0, 4);
    Func reduced{"reduced"}, consumer{"consumer"};
    reduced(x) = 0;
    reduced(x) += r;  // update definition -> not pure
    consumer(x) = reduced(x);

    bool error = false;
    try {
        consumer.eager_inline({reduced});
    } catch (const Halide::CompileError &e) {
        error = true;
        printf("Expected error (cannot inline impure Func):\n%s\n", e.what());
    }
    if (!error) {
        printf("eager_inline should have rejected a Func with an update definition!\n");
        return 1;
    }
    return 0;
}
#endif

}  // namespace

int main(int argc, char **argv) {
    printf("Running eager_inline_exposes_factor_test\n");
    if (eager_inline_exposes_factor_test()) {
        return 1;
    }
    printf("Running eager_inline_chain_test\n");
    if (eager_inline_chain_test()) {
        return 1;
    }
#if HALIDE_WITH_EXCEPTIONS
    printf("Running eager_inline_non_pure_rejected_test\n");
    if (eager_inline_non_pure_rejected_test()) {
        return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
