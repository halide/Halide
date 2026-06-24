#include "Halide.h"
#include <cstdio>
#include <vector>

// Stress test for skip_stages on a Func whose value is read via a long chain
// of CSE'd let bindings, each carrying a Select gated on an independent
// scalar Param. The setup:
//   - f has a self-referencing update definition (so it gets
//     compute_at(innermost) and shows up in conditionally_used_funcs).
//   - A chain of derived Exprs over f is built, each referenced multiple
//     times downstream so CSE materialises them as a let chain.
//   - Each chain value contains a Select gated on a Param<bool>.
//
// This pattern used to scale exponentially in skip_stages: every nested
// Select roughly doubled the size of the .used / .loaded predicate that
// the mutator built up for f, because the boolean form
// `(t && cond) || (f && !cond)` couldn't recognise that the t and f
// sub-predicates were the same Expr coming from a let-stashed FuncInfo
// above. At this chain length the predicate would contain ~2^500 IR
// nodes -- i.e. skip_stages would crash trying to allocate it long
// before any wall-clock timeout fired. Post-fix it lowers in a fraction
// of a second.

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), c("c");

    ImageParam src(Float(32), 3, "src");

    constexpr int num_params = 8;
    std::vector<Param<bool>> conds;
    conds.reserve(num_params);
    for (int i = 0; i < num_params; i++) {
        conds.emplace_back("cond" + std::to_string(i));
    }

    // f: self-referencing update -> can't be inlined, becomes a separate
    // compute_at(innermost) Func, so it shows up in skip_stages's analysis.
    Func f("f");
    f(x, y, c) = src(x, y, c) * 1.5f + 0.5f;
    f(x, y, c) = clamp(f(x, y, c), 0.0f, 1.0f);

    // Build a long chain of derived expressions. Each entry references
    // the immediately preceding one (chain.back()) plus two pseudo-random
    // earlier ones. The dependency on chain.back() guarantees that every
    // entry is reachable from the final one, so nothing gets dropped as
    // dead. CSE will materialise each entry as a let in the lowered IR.
    // Each chain entry's value contains a Select gated on one of the
    // Param<bool>s.
    constexpr int chain_len = 500;
    std::vector<Expr> chain;
    chain.reserve(chain_len + 3);
    chain.push_back(f(x, y, 0));
    chain.push_back(f(x, y, 1));
    chain.push_back(f(x, y, 2));
    for (int i = 0; i < chain_len; i++) {
        Expr a = chain.back();
        Expr b = chain[(i * 5 + 1) % chain.size()];
        Expr d = chain[(i * 7 + 2) % chain.size()];
        Expr cond = conds[i % num_params];
        Expr e = select(cond, a * b + d, (a - d) * b) +
                 cast<float>(i) * 0.0001f;
        chain.push_back(e);
    }

    Func out("out");
    out(x, y) = chain.back();
    out.compile_jit(get_jit_target_from_environment());
    printf("Success!\n");
    return 0;
}
