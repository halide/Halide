#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

TEST(HoistLoopInvariantIfStatements, IfStatementHoisting) {
    Func f, g;
    Var x, y;
    Param<bool> p;

    // In Stmt IR, if statements can be injected by GuardWithIf, RDom
    // predicates, specializations, and uses of undef. There are
    // various situations where an if statement can end up further
    // inside a loop nest than strictly necessary. Here's one:

    f(x, y) = select(p, x + y, undef<int>());
    g(x, y) = select(p, f(x, y), undef<int>());
    f.compute_at(g, x);

    // Both f and g get an if statement for p, which could instead be
    // a single combined top-level if statement. Trim-no-ops is
    // supposed to lift the if statement out of the loops to the top
    // level. Let's check if it worked.

    struct : IRMutator {
        bool in_loop = false;
        bool if_in_loop = false;
        Stmt visit(const For *op) override {
            ScopedValue<bool> old(in_loop, true);
            return IRMutator::visit(op);
        }
        Stmt visit(const IfThenElse *op) override {
            if_in_loop |= in_loop;
            return IRMutator::visit(op);
        }
    } checker;
    g.add_custom_lowering_pass(&checker, nullptr);

    p.set(true);
    g.realize({1024, 1024});

    EXPECT_FALSE(checker.if_in_loop) << "Found an if statement inside a loop. This was not supposed to happen";
}
