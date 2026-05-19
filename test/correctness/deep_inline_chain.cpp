#include "Halide.h"

using namespace Halide;

// Stress-test for the inliner on a long chain of inline-scheduled Funcs.
//
// The test builds a sequence of Funcs where each new Func calls its previous
// 10 predecessors (at two different values of an extra coordinate) and
// passes the sum through a fresh per-level lookup-table Func. All Funcs are
// left at their default schedule (compute_inline), so every one of them
// must be inlined into the output during lowering.
//
// Before src/Inline.cpp learned to batch and CSE between batches, this was
// exponentially expensive in the chain length: ScheduleFunctions inlined
// one Func at a time, paying O(N) walks of 's', and BoundsInference inlined
// every Func at once into a giant Let-nested DAG that CSE's RemoveLets then
// re-walked exponentially under each Let. With the batched/iterative-
// deepening Inliner, lowering this pipeline takes well under a second.
//
// Failure modes this test guards against:
//   - The test hangs or times out in CI: a regression has reintroduced the
//     exponential/quadratic lowering cost.
//   - The test crashes during JIT compilation or execution: the inliner
//     produced malformed IR.
//   - The test prints "Mismatch": the inliner produced incorrect IR that
//     still lowers and runs, but computes the wrong value.
//
// The mismatch case is checked by computing one output value out-of-band
// (by walking the Func vector at C++ level) and comparing.
int main(int argc, char **argv) {
    Var x, c;

    // Reference computation, run in C++ alongside the pipeline build to
    // give us an expected output value to compare against.
    std::vector<std::vector<int>> ref;  // ref[i][c] == funcs[i](x=0, c)

    std::vector<Func> funcs;
    auto add_leaf = [&](Func f, int v0, int v1) {
        funcs.push_back(std::move(f));
        ref.push_back({v0, v1});
    };
    add_leaf(lambda(x, c, x + c), 0, 1);
    add_leaf(lambda(x, c, x + c + 1), 1, 2);
    add_leaf(lambda(x, c, x + c + 2), 2, 3);
    add_leaf(lambda(x, c, x + c + 3), 3, 4);

    // Number of layers added on top of the four leaf Funcs. Each layer is
    // one Func, with a unique LUT also inlined into it. 100 layers is
    // plenty to exercise the batched Inliner (the batch size is 8, so this
    // produces many sequential batches).
    const int N = 100;

    for (int i = 0; i < N; i++) {
        Func next, lut;
        lut(x) = x * x + i;
        Expr e = 0;
        int e_ref = 0;
        for (int k = 0; k < 10 && k < (int)funcs.size(); k++) {
            Func &f = funcs[funcs.size() - 1 - k];
            e += f(x, 0) * f(x, 1);
            const auto &fr = ref[ref.size() - 1 - k];
            e_ref += fr[0] * fr[1];
        }
        next(x, c) = lut(e);
        funcs.push_back(std::move(next));
        // lut(e_ref) = e_ref * e_ref + i. next doesn't depend on c, so
        // both c=0 and c=1 give the same value.
        int v = e_ref * e_ref + i;
        ref.push_back({v, v});
    }

    Buffer<int> out = funcs.back().realize({1, 2});
    int expected = ref.back()[0];
    if (out(0, 0) != expected || out(0, 1) != expected) {
        printf("Mismatch: got (%d, %d), expected (%d, %d)\n",
               out(0, 0), out(0, 1), expected, expected);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
