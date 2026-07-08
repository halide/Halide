#include "Halide.h"
#include <stdio.h>
#include <string>
#include <utility>

using namespace Halide;
using namespace Halide::Internal;

// Test select(cond, a, b).branch(), which turns the branchless Select
// (both sides evaluated) into the if_then_else intrinsic that lowers to a
// real control-flow branch evaluating only the taken side.

namespace {

// Count the number of Calls with the given name in an Expr (counts textual
// occurrences, so a subexpression reached through two parents counts twice).
class CountNamedCall : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) override {
        if (op->name == name) {
            count++;
        }
        IRVisitor::visit(op);
    }

public:
    std::string name;
    int count = 0;
    explicit CountNamedCall(std::string n)
        : name(std::move(n)) {
    }
};

int count_named_calls(const Expr &e, const std::string &name) {
    CountNamedCall c(name);
    e.accept(&c);
    return c.count;
}

// Walk a lowered body, counting a target named Call both in total and within
// the true arm of any scalar if_then_else. Used to prove the CSE barrier
// keeps arm-only subexpressions inside the branch.
class BranchBarrierCheck : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) override {
        if (op->name == target) {
            total++;
        }
        if (op->is_intrinsic(Call::if_then_else) && op->args.size() == 3) {
            found_ite = true;
            in_true_arm += count_named_calls(op->args[1], target);
        }
        IRVisitor::visit(op);
    }

public:
    std::string target;
    bool found_ite = false;
    int total = 0;
    int in_true_arm = 0;
    explicit BranchBarrierCheck(std::string t)
        : target(std::move(t)) {
    }
};

}  // namespace

int main(int argc, char **argv) {
    Var x("x");

    // Part A: the frontend IR shape.
    {
        Expr cond = x > 5;
        Expr t = x * 2;
        Expr f = x + 1;

        Expr s = select(cond, t, f);
        const Select *sel = s.as<Select>();
        if (!sel) {
            printf("select() did not produce a Select node\n");
            return 1;
        }

        Expr b = s.branch();
        const Call *c = Call::as_intrinsic(b, {Call::if_then_else});
        if (!c) {
            printf("branch() did not produce an if_then_else intrinsic\n");
            return 1;
        }
        if (c->args.size() != 3 ||
            !equal(c->args[0], cond) ||
            !equal(c->args[1], t) ||
            !equal(c->args[2], f)) {
            printf("branch() did not preserve the select's operands\n");
            return 1;
        }
        if (b.type() != s.type()) {
            printf("branch() changed the result type\n");
            return 1;
        }
    }

    // Part B: branch() is numerically equivalent to select() end-to-end.
    {
        Expr cond = (x % 3) == 0;
        Expr t = x * x + 7;
        Expr f = x * 5 - 2;

        Func sel("sel"), br("br");
        sel(x) = select(cond, t, f);
        br(x) = select(cond, t, f).branch();

        Buffer<int> rs = sel.realize({256});
        Buffer<int> rb = br.realize({256});
        for (int i = 0; i < rs.width(); i++) {
            if (rs(i) != rb(i)) {
                printf("Mismatch at %d: select=%d branch=%d\n", i, rs(i), rb(i));
                return 1;
            }
        }
    }

    // Part C: the multi-way select becomes a full chain of branches - the
    // nested tail is recursively converted to if_then_else too.
    {
        Expr e = select(x < 10, 1, x < 20, 2, 3).branch();
        const Call *c = Call::as_intrinsic(e, {Call::if_then_else});
        if (!c) {
            printf("multi-way branch() did not produce an if_then_else\n");
            return 1;
        }
        // The false branch is the recursively-branched tail, not a select.
        if (c->args[2].as<Select>()) {
            printf("multi-way branch() should recurse into the tail\n");
            return 1;
        }
        const Call *tail = Call::as_intrinsic(c->args[2], {Call::if_then_else});
        if (!tail) {
            printf("multi-way branch() tail should be an if_then_else\n");
            return 1;
        }

        Func f("multi");
        f(x) = e;
        Buffer<int> r = f.realize({30});
        for (int i = 0; i < r.width(); i++) {
            int correct = i < 10 ? 1 : (i < 20 ? 2 : 3);
            if (r(i) != correct) {
                printf("multi-way mismatch at %d: got %d want %d\n", i, r(i), correct);
                return 1;
            }
        }
    }

    // Part E: the CSE barrier keeps arm-only subexpressions inside the branch
    // rather than hoisting them into a let that runs unconditionally. An
    // "expensive" pure extern call is used twice in the true arm; ordinarily
    // CSE would lift it into a let above the branch, but branch()'s barrier
    // must keep it inside the arm.
    {
        Expr e = Call::make(Int(32), "expensive_fn", {x}, Call::PureExtern);
        Expr arm = min(e, 5) + max(e, 7);  // uses e twice, not foldable away

        Func f("barrier");
        f(x) = select(x > 3, arm, 0).branch();

        Module m = f.compile_to_module({}, "barrier");

        BranchBarrierCheck check("expensive_fn");
        for (const auto &fn : m.functions()) {
            fn.body.accept(&check);
        }

        if (!check.found_ite) {
            printf("barrier test: no if_then_else survived lowering\n");
            return 1;
        }
        if (check.in_true_arm < 1) {
            printf("barrier failed: expensive call was hoisted out of the arm\n");
            return 1;
        }
        // Every occurrence of the expensive call must be inside the arm; if
        // any were hoisted into an enclosing let, total would exceed in_arm.
        if (check.total != check.in_true_arm) {
            printf("barrier failed: %d expensive calls total but only %d in the arm\n",
                   check.total, check.in_true_arm);
            return 1;
        }
    }

    // Part D: calling branch() on something that isn't a select is an error.
#ifdef HALIDE_WITH_EXCEPTIONS
    {
        bool threw = false;
        try {
            Expr not_a_select = x + 1;
            (void)not_a_select.branch();
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch() on a non-select should have thrown\n");
            return 1;
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
