// A fact in scope must not change how an unrelated expression folds.
//
// Written in the style of test/correctness/simplify.cpp so it can be folded
// into check_bounds() there.
//
//   g++ -std=c++17 -I <build>/include simplify_region_bound_regression.cpp \
//       -L <build>/src -lHalide -Wl,-rpath,<build>/src -o t && ./t
//
// Where this comes from
// ---------------------
// Lowering apps/local_laplacian at pyramid_levels=6 widens two allocations
// relative to main. Reduced, the cause is below.
//
// The pyramid bound is let-bound once, near the top of the IR, where no fact is
// in scope:
//
//   let gPyramid4.s0.v1.max = max((E + 14)/16, (((E + 30)/32)*2) + 2)
//
// and a copy of that same expression appears further in, inside an if, where
// facts *are* in scope. Peeling constant divisors lets known_difference settle
// (E + 14)/16 against (((E + 30)/32)*2) + 2 by arithmetic alone -- but the
// min/max rules that consult it are gated on has_difference_facts(), so the
// copy inside the if folds to its second arm and the let-bound one does not.
//
// The two copies are then no longer spelled the same, and that is what matters
// here: a LetStmt-bound variable is never substituted into the body (single-use
// inlining is disabled for Stmt bodies), and a written-out copy of a let's value
// is not recognised and rewritten into the variable. So
//
//   max(<written-out bound>, min(Q, gPyramid4.s0.v1.max))
//
// can only collapse while both copies are still the same expression. Once one
// of them folds and the other does not, nothing collapses, and the surviving
// min/max widens the allocation.
//
// On main neither copy folds -- there is no such prover at all -- so the two
// stay equal and everything downstream works. Firing in *some* scopes is what
// breaks it, not firing as such.
//
// The fold itself is correct; only its inconsistency is the problem.

#include "Halide.h"
#include <cstdio>
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

namespace {

int failures = 0;

// Simplify e with no fact in scope, and again with an unrelated fact in scope.
// The two must agree.
Stmt sink(const Expr &x) {
    return Evaluate::make(Call::make(Int(32), "sink", {x}, Call::Extern));
}

void check_fact_independent(const Expr &e) {
    Expr p = Variable::make(Int(32), "p");
    Expr q = Variable::make(Int(32), "q");

    Stmt bare = simplify(sink(e));
    Stmt guarded = simplify(IfThenElse::make(p < q, sink(e)));

    // Dig the simplified expression back out of each.
    const Evaluate *bare_eval = bare.as<Evaluate>();
    const IfThenElse *ite = guarded.as<IfThenElse>();
    if (!bare_eval || !ite) {
        std::cerr << "test is malformed\n";
        failures++;
        return;
    }
    const Evaluate *guarded_eval = ite->then_case.as<Evaluate>();
    if (!guarded_eval) {
        std::cerr << "test is malformed\n";
        failures++;
        return;
    }

    Expr without = bare_eval->value.as<Call>()->args[0];
    Expr with = guarded_eval->value.as<Call>()->args[0];

    if (!equal(without, with)) {
        std::cerr << "\nA fact in scope changed an unrelated simplification:\n"
                  << "Input:            " << e << "\n"
                  << "Without a fact:   " << without << "\n"
                  << "With p < q:       " << with << "\n";
        failures++;
    }
}

// Simplify a statement and compare against the expected result.
void check_stmt(const Stmt &a, const Stmt &b) {
    std::cerr << "----\n";
    std::cerr << "Input:\n"
              << a << "\n";
    Stmt simpler = simplify(a);
    std::cerr << "\nOutput:\n"
              << simpler << "\n";
    if (!equal(simpler, b)) {
        std::cerr << "\nSimplification failure:\n"
                  << "Expected output:\n"
                  << b << "\n";
        failures++;
    } else {
        std::cerr << "Ok!\n";
    }
}

}  // namespace

int main() {
    // Facts are only learned once lowering has finished reading regions out of
    // the IR, so without this nothing is learned and the test is vacuous.
    // ScopedRegionsInferred regions_inferred;

    Expr e = Variable::make(Int(32), "e");  // output.extent.1
    Expr m = Variable::make(Int(32), "m");  // output.min.1
    Expr E = e + m;

    // gPyramid4.s0.v1.max, verbatim. Folds to its second arm with a fact in
    // scope and stays put without one.
    check_fact_independent(max((E + 14) / 16, (((E + 30) / 32) * 2) + 2));

    // The v0 counterpart, /8 and /16.
    check_fact_independent(max((E + 6) / 8, (((E + 14) / 16) * 2) + 2));

    // gPyramid4.s0.v1.min, the same shape for min.
    check_fact_independent(min((m + -15) / 16, (((m + -31) / 32) * 2) + -1));

    // The consequence. In the lowered IR the two copies of the bound are not
    // in the same scope: one is written out, the other is a reference to a
    // LetStmt-bound variable whose binding sits outside the `if`, where no fact
    // is in scope.
    //
    //   let V = max(A1, A2)                <- no fact in scope here
    //     if (p < q) {                     <- facts in scope here
    //       sink(max(max(A1, A2), min(Q, V)))
    //     }
    //
    // A LetStmt-bound variable is never substituted into the body -- single-use
    // inlining is disabled for Stmt bodies -- and a written-out copy of a let's
    // value is not recognised and rewritten into the variable. So V stays
    // opaque, and the collapse to a single term needs both copies to still be
    // spelled the same. If the fold fires on the written-out copy but not on
    // V's value, they differ and nothing collapses.
    Expr p = Variable::make(Int(32), "p");
    Expr q = Variable::make(Int(32), "q");
    Expr Q = Variable::make(Int(32), "Q");
    Expr V = Variable::make(Int(32), "V");
    Expr bound = max((E + 14) / 16, (((E + 30) / 32) * 2) + 2);
    Expr folded = (((E + 30) / 32) * 2) + 2;

    // Both copies written out, without any facts, in one scope: symmetric, and it collapses.
    check_stmt(sink(max(bound, min(Q, bound))),
               sink(folded));

    // Both copies written out, in one scope: symmetric, and it collapses.
    check_stmt(IfThenElse::make(p < q, sink(max(bound, min(Q, bound)))),
               IfThenElse::make(p < q, sink(folded)));

    // One copy behind a LetStmt bound outside the `if`: the shape from the IR.
    check_stmt(LetStmt::make("V", bound,
                             IfThenElse::make(p < q, sink(max(bound, min(Q, V))))),
               IfThenElse::make(p < q, sink(folded)));

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
