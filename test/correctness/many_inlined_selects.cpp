#include "Halide.h"
#include <cstdio>
#include <vector>

// Stress test for skip_stages on a Func whose value is read via a long chain
// of CSE'd let bindings, each carrying a Select gated on an independent
// scalar Param. The setup:
//   - F has a self-referencing update definition (so it gets
//     compute_at(innermost) and shows up in conditionally_used_funcs).
//   - A chain of derived Exprs over F is built, each referenced multiple
//     times downstream so CSE materialises them as a let chain.
//   - Each chain value contains a Select gated on a Param<bool>.
//   - A final Select feeds the whole chain into the output.
//
// This pattern used to scale exponentially in skip_stages: every nested
// Select roughly doubled the size of the .used / .loaded predicate that
// the mutator built up for F, because the boolean form
// `(t && cond) || (f && !cond)` couldn't recognise that the t and f
// sub-predicates were the same Expr coming from a let-stashed FuncInfo
// above. At this chain length the predicate would contain ~2^500 IR
// nodes — i.e. skip_stages would crash trying to allocate it long
// before any wall-clock timeout fired. Post-fix it lowers in a fraction
// of a second.

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), c("c");

    ImageParam src(Float(32), 3, "src");
    Param<bool> gate("gate");

    constexpr int num_params = 8;
    std::vector<Param<bool>> conds;
    conds.reserve(num_params);
    for (int i = 0; i < num_params; i++) {
        conds.emplace_back("cond" + std::to_string(i));
    }

    // F: self-referencing update -> can't be inlined, becomes a separate
    // compute_at(innermost) Func, so it shows up in skip_stages's analysis.
    Func F("F");
    F(x, y, c) = src(x, y, c) * 1.5f + 0.5f;
    F(x, y, c) = clamp(F(x, y, c), 0.0f, 1.0f);

    // -- Build a long chain of derived expressions. Each Expr is held in a
    // C++ variable and referenced multiple times by subsequent Exprs, so
    // CSE will materialise each as a let in the lowered IR.
    constexpr int chain_len = 500;
    std::vector<Expr> chain;
    chain.reserve(chain_len + 3);
    chain.push_back(F(x, y, 0));
    chain.push_back(F(x, y, 1));
    chain.push_back(F(x, y, 2));
    for (int i = 0; i < chain_len; i++) {
        Expr a = chain[(i * 3) % chain.size()];
        Expr b = chain[(i * 5 + 1) % chain.size()];
        Expr d = chain[(i * 7 + 2) % chain.size()];
        // Each chain entry's value contains a select gated on one of the
        // Param<bool>s. The let cascade in skip_stages's analysis marks
        // the whole chain as interesting and the mutator builds a
        // predicate for each let in turn.
        Expr cond = conds[i % num_params];
        Expr e = select(cond, a * b + d, (a - d) * b) +
                 cast<float>(i) * 0.0001f;
        chain.push_back(e);
    }

    // -- Final expression is a single select gated on a runtime param.
    // The branches reference the *last* (and therefore transitively every)
    // chain entry, so the in-condition cascade marks every let interesting.
    Expr branch_t = chain.back() + chain[chain.size() / 2];
    Expr branch_f = chain.back() - chain[chain.size() / 2];
    Func out("out");
    out(x, y) = select(gate, branch_t, branch_f);

    Target target = get_jit_target_from_environment();
    int vec = target.natural_vector_size<float>();
    Var tx("tx"), yo("yo"), yi("yi");
    out.split(x, x, tx, vec, TailStrategy::GuardWithIf).vectorize(tx)
       .split(y, yo, yi, 64, TailStrategy::GuardWithIf).parallel(yo);

    out.compile_jit(target);
    printf("Success!\n");
    return 0;
}
