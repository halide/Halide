#include "Halide.h"
#include <stdio.h>
#include <string>
#include <utility>

using namespace Halide;
using namespace Halide::Internal;

// Test branch(cond, a, b): a strict variant of select that lowers to a real
// control-flow branch evaluating only the taken side. Unlike select(), branch()
// returns a Branch (not an Expr) and may only be used as the whole right-hand
// side of a Func definition. Its value arms are force-inlined so the branch
// gates real computation, not just a load.

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

// True if the lowered body allocates a buffer whose name starts with `prefix`
// (i.e. the corresponding Func was materialized rather than inlined).
class HasAllocationPrefixed : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Allocate *op) override {
        if (op->name.rfind(prefix, 0) == 0) {
            found = true;
        }
        IRVisitor::visit(op);
    }

public:
    std::string prefix;
    bool found = false;
    explicit HasAllocationPrefixed(std::string p)
        : prefix(std::move(p)) {
    }
};

bool module_allocates(const Module &m, const std::string &prefix) {
    HasAllocationPrefixed v(prefix);
    for (const auto &fn : m.functions()) {
        fn.body.accept(&v);
    }
    return v.found;
}

}  // namespace

int main(int argc, char **argv) {
    Var x("x");

    // Part A: branch() returns a Branch wrapping the branch intrinsic, with the
    // operands preserved.
    {
        Expr cond = x > 5;
        Expr t = x * 2;
        Expr f = x + 1;

        Branch b = branch(cond, t, f);
        const Call *c = Call::as_intrinsic(b.expr, {Call::branch});
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
        Branch e = branch(x < 10, 1, x < 20, 2, 3);
        const Call *c = Call::as_intrinsic(e.expr, {Call::branch});
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

    // Part E: the value arms are force-inlined, so a branch over Func calls
    // does NOT materialize the producers - only the taken side is computed.
    {
        Func arm_g("arm_g"), arm_h("arm_h"), f("force_inline");
        arm_g(x) = x * x + 7;
        arm_h(x) = x * 5 - 2;
        f(x) = branch(x < 128, arm_g(x), arm_h(x));

        Module m = f.compile_to_module({}, "force_inline");
        if (module_allocates(m, "arm_g") || module_allocates(m, "arm_h")) {
            printf("force-inline failed: an arm Func was materialized, not inlined\n");
            return 1;
        }

        Buffer<int> r = f.realize({256});
        for (int i = 0; i < r.width(); i++) {
            int correct = i < 128 ? (i * i + 7) : (i * 5 - 2);
            if (r(i) != correct) {
                printf("force-inline mismatch at %d: got %d want %d\n", i, r(i), correct);
                return 1;
            }
        }
    }

    // Part F: force-inline overrides an explicit compute_root on an arm Func.
    {
        Func arm_g("fi_g"), arm_h("fi_h"), f("force_override");
        arm_g(x) = x * 3;
        arm_h(x) = x + 9;
        arm_g.compute_root();  // branch() must override this
        f(x) = branch(x < 64, arm_g(x), arm_h(x));

        Module m = f.compile_to_module({}, "force_override");
        if (module_allocates(m, "fi_g")) {
            printf("force-inline did not override compute_root on an arm Func\n");
            return 1;
        }
    }

    // Part G: branch() is allowed inside a GPU kernel, where it becomes a real
    // per-thread branch. Runs on the device when one is available (e.g. under a
    // d3d12compute jit target), comparing the result against select().
    {
        Target t = get_jit_target_from_environment();
        if (t.has_gpu_feature()) {
            Var xo("xo"), xi("xi");
            Func gsel("gsel"), gbr("gbr");
            gsel(x) = select(x % 2 == 0, x * x + 7, x * 5 - 2);
            gbr(x) = branch(x % 2 == 0, x * x + 7, x * 5 - 2);
            gsel.gpu_tile(x, xo, xi, 16);
            gbr.gpu_tile(x, xo, xi, 16);
            Buffer<int> rs = gsel.realize({256}, t);
            Buffer<int> rb = gbr.realize({256}, t);
            rs.copy_to_host();
            rb.copy_to_host();
            for (int i = 0; i < 256; i++) {
                if (rs(i) != rb(i)) {
                    printf("GPU mismatch at %d: select=%d branch=%d\n", i, rs(i), rb(i));
                    return 1;
                }
            }
        } else {
            printf("[gpu part skipped: jit target has no gpu feature]\n");
        }
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    // Part H: branch() with a lane-varying (vector) condition is a hard error
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

    // Part I: a branch whose condition depends on a vectorized dimension is a
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

    // Part J: branch() combined with vectorization inside a GPU kernel is a
    // hard error (only per-thread / per-wave non-vectorized branches allowed).
    {
        Param<int> p("p");
        Func h("gpu_vec_branch");
        Var xo("xo"), xi("xi"), xv("xv");
        h(x) = branch(p > 0, x * 2, x + 1);
        h.split(x, xo, xi, 64)
            .split(xi, xi, xv, 4)
            .gpu_blocks(xo)
            .gpu_threads(xi)
            .vectorize(xv);
        bool threw = false;
        try {
            h.compile_to_module({p}, "gpu_vec_branch",
                                get_host_target().with_feature(Target::CUDA));
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch() with vectorization inside a GPU kernel should have thrown\n");
            return 1;
        }
    }

    // Part K: a value arm that can not be inlined (a Func with an update stage)
    // is a hard error at definition time.
    {
        bool threw = false;
        try {
            Func g("upd");
            g(x) = 0;
            RDom r(0, 4);
            g(x) += r;  // update stage -> not inlinable
            Func f("uses_update");
            f(x) = branch(x < 5, g(x), 0);
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch() over a Func with an update stage should have thrown\n");
            return 1;
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
