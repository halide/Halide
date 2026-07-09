#include "Halide.h"
#include <stdio.h>
#include <string>
#include <utility>

using namespace Halide;
using namespace Halide::Internal;

// Test branch(cond, a, b): a strict variant of select that lowers to a real
// control-flow branch evaluating only the taken side, instead of the branchless
// select that always evaluates both sides.

namespace {

// Count Calls with the given name in an Expr (textual occurrences).
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

// Walk a lowered body, counting a target named Call in total and within the
// true arm of any scalar if_then_else (branch lowers to if_then_else). Used to
// prove the CSE barrier keeps arm-only subexpressions inside the branch.
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

    // Part A: the frontend produces the branch intrinsic and keeps operands.
    {
        Expr cond = x > 5;
        Expr t = x * 2;
        Expr f = x + 1;

        Expr b = branch(cond, t, f);
        const Call *c = Call::as_intrinsic(b, {Call::branch});
        if (!c) {
            printf("branch() did not produce a branch intrinsic\n");
            return 1;
        }
        if (c->args.size() != 3 ||
            !equal(c->args[0], cond) ||
            !equal(c->args[1], t) ||
            !equal(c->args[2], f)) {
            printf("branch() did not preserve its operands\n");
            return 1;
        }
        if (b.type() != t.type()) {
            printf("branch() has the wrong result type\n");
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
        br(x) = branch(cond, t, f);

        Buffer<int> rs = sel.realize({256});
        Buffer<int> rb = br.realize({256});
        for (int i = 0; i < rs.width(); i++) {
            if (rs(i) != rb(i)) {
                printf("Mismatch at %d: select=%d branch=%d\n", i, rs(i), rb(i));
                return 1;
            }
        }
    }

    // Part C: the multi-way branch becomes a chain of branch intrinsics.
    {
        Expr e = branch(x < 10, 1, x < 20, 2, 3);
        const Call *c = Call::as_intrinsic(e, {Call::branch});
        if (!c) {
            printf("multi-way branch() did not produce a branch intrinsic\n");
            return 1;
        }
        const Call *tail = Call::as_intrinsic(c->args[2], {Call::branch});
        if (!tail) {
            printf("multi-way branch() tail should itself be a branch\n");
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

    // Part D: the CSE barrier keeps arm-only subexpressions inside the branch
    // rather than hoisting them into a let that runs unconditionally.
    {
        Expr e = Call::make(Int(32), "expensive_fn", {x}, Call::PureExtern);
        Expr arm = min(e, 5) + max(e, 7);  // uses e twice, not foldable away

        Func f("barrier");
        f(x) = branch(x > 3, arm, 0);

        Module m = f.compile_to_module({}, "barrier");

        BranchBarrierCheck check("expensive_fn");
        for (const auto &fn : m.functions()) {
            fn.body.accept(&check);
        }

        if (!check.found_ite) {
            printf("barrier test: branch did not lower to an if_then_else\n");
            return 1;
        }
        if (check.in_true_arm < 1) {
            printf("barrier failed: expensive call was hoisted out of the arm\n");
            return 1;
        }
        if (check.total != check.in_true_arm) {
            printf("barrier failed: %d expensive calls total but only %d in the arm\n",
                   check.total, check.in_true_arm);
            return 1;
        }
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    // Part E: branch() with a lane-varying (vector) condition is a hard error
    // at construction (scalar condition only).
    {
        bool threw = false;
        try {
            Expr vec_cond = Broadcast::make(x > 5, 4);
            (void)branch(vec_cond, x * 2, x + 1);
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch() with a vector condition should have thrown\n");
            return 1;
        }
    }

    // Part F: a branch whose condition depends on a vectorized dimension is a
    // hard error at lowering (no silent fallback to select).
    {
        Func f("vec_branch");
        f(x) = branch(x < 5, x * 2, x + 1);
        f.vectorize(x, 8);
        bool threw = false;
        try {
            f.compile_to_module({}, "vec_branch");
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("vectorized branch() should have thrown at lowering\n");
            return 1;
        }
    }

    // Part G: a branch inside a GPU kernel is a hard error at lowering.
    {
        Func g("gpu_branch");
        g(x) = branch(x < 5, x * 2, x + 1);
        Var xo("xo"), xi("xi");
        g.gpu_tile(x, xo, xi, 16);
        bool threw = false;
        try {
            g.compile_to_module({}, "gpu_branch", get_host_target().with_feature(Target::CUDA));
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch() inside a GPU kernel should have thrown at lowering\n");
            return 1;
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
