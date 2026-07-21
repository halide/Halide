#include "Halide.h"
#include <cmath>
#include <stdio.h>
#include <string>
#include <utility>

using namespace Halide;
using namespace Halide::Internal;

// Test branch(cond, a, b): a strict variant of select. branch() returns a
// Branch (not an Expr) and may only be the whole right-hand side of a Func
// definition. It is turned into real point-wise control flow in
// ScheduleFunctions (an IfThenElse with one store per arm, like specialize but
// point-wise), hoisted to the outermost loop level where its condition is
// invariant, so producers can be compute_at'd inside it and have their
// computation gated by the branch.

namespace {

// Is there a Stmt-level IfThenElse (with a real else) anywhere in the body?
class HasRealBranch : public IRVisitor {
    using IRVisitor::visit;
    void visit(const IfThenElse *op) override {
        if (op->else_case.defined()) {
            found = true;
        }
        IRVisitor::visit(op);
    }

public:
    bool found = false;
};

// Is a ProducerConsumer "produce <name>" nested inside an IfThenElse? That is
// what tells us the producer's computation is gated by the branch.
class ProduceInsideBranch : public IRVisitor {
    using IRVisitor::visit;

    int depth = 0;

    void visit(const IfThenElse *op) override {
        if (op->else_case.defined()) {
            depth++;
            IRVisitor::visit(op);
            depth--;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const ProducerConsumer *op) override {
        if (op->is_producer && op->name == target && depth > 0) {
            found = true;
        }
        IRVisitor::visit(op);
    }

public:
    std::string target;
    bool found = false;
    explicit ProduceInsideBranch(std::string t)
        : target(std::move(t)) {
    }
};

template<typename V>
bool scan_module(const Module &m, V &v) {
    for (const auto &fn : m.functions()) {
        fn.body.accept(&v);
    }
    return v.found;
}

// Count Stmt-level IfThenElse nodes that have a real else (i.e. branches).
class CountRealBranches : public IRVisitor {
    using IRVisitor::visit;
    void visit(const IfThenElse *op) override {
        if (op->else_case.defined()) {
            count++;
        }
        IRVisitor::visit(op);
    }

public:
    int count = 0;
};

int count_real_branches(const Module &m) {
    CountRealBranches v;
    for (const auto &fn : m.functions()) {
        fn.body.accept(&v);
    }
    return v.count;
}

// Count "produce <target>" nodes inside a Stmt.
class CountProduce : public IRVisitor {
    using IRVisitor::visit;
    void visit(const ProducerConsumer *op) override {
        if (op->is_producer && op->name == target) {
            count++;
        }
        IRVisitor::visit(op);
    }

public:
    std::string target;
    int count = 0;
    explicit CountProduce(std::string t)
        : target(std::move(t)) {
    }
};

int count_produce(const Stmt &s, const std::string &name) {
    CountProduce v(name);
    s.accept(&v);
    return v.count;
}

// For the outermost real branch, how many times is "produce <target>" found in
// its then-side vs its else-side. Used to check a producer is only computed in
// the arm that actually uses it.
class BranchArmProduce : public IRVisitor {
    using IRVisitor::visit;
    void visit(const IfThenElse *op) override {
        if (!found && op->else_case.defined()) {
            found = true;
            then_count = count_produce(op->then_case, target);
            else_count = count_produce(op->else_case, target);
            return;
        }
        IRVisitor::visit(op);
    }

public:
    std::string target;
    bool found = false;
    int then_count = 0, else_count = 0;
    explicit BranchArmProduce(std::string t)
        : target(std::move(t)) {
    }
};

BranchArmProduce arm_produce(const Module &m, const std::string &name) {
    BranchArmProduce v(name);
    for (const auto &fn : m.functions()) {
        fn.body.accept(&v);
    }
    return v;
}

// True if a real branch appears nested inside any For loop.
class BranchInsideAnyLoop : public IRVisitor {
    using IRVisitor::visit;

    int depth = 0;

    void visit(const For *op) override {
        depth++;
        IRVisitor::visit(op);
        depth--;
    }

    void visit(const IfThenElse *op) override {
        if (op->else_case.defined() && depth > 0) {
            found = true;
        }
        IRVisitor::visit(op);
    }

public:
    bool found = false;
};

// True if some branch (IfThenElse with a real else) has a For loop inside it,
// i.e. the branch was hoisted above that loop.
class BranchAboveLoop : public IRVisitor {
    using IRVisitor::visit;

    bool in_branch = false;

    void visit(const IfThenElse *op) override {
        if (op->else_case.defined()) {
            bool old = in_branch;
            in_branch = true;
            IRVisitor::visit(op);
            in_branch = old;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const For *op) override {
        if (in_branch) {
            found = true;
        }
        IRVisitor::visit(op);
    }

public:
    bool found = false;
};

}  // namespace

int main(int argc, char **argv) {
    Var x("x"), y("y");

    // Part A: branch() returns a Branch wrapping the branch intrinsic.
    {
        Expr cond = x > 5;
        Expr t = x * 2;
        Expr f = x + 1;

        Branch b = branch(cond, t, f);
        const Call *c = Call::as_intrinsic(b.expr, {Call::branch});
        if (!c || c->args.size() != 3 ||
            !equal(c->args[0], cond) || !equal(c->args[1], t) || !equal(c->args[2], f)) {
            printf("branch() did not produce a well-formed branch intrinsic\n");
            return 1;
        }
    }

    // Part B: branch() is numerically equivalent to select().
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

    // Part C: the multi-way branch produces correct values.
    {
        Func f("multi");
        f(x) = branch(x < 10, 1, x < 20, 2, 3);
        Buffer<int> r = f.realize({30});
        for (int i = 0; i < r.width(); i++) {
            int correct = i < 10 ? 1 : (i < 20 ? 2 : 3);
            if (r(i) != correct) {
                printf("multi-way mismatch at %d: got %d want %d\n", i, r(i), correct);
                return 1;
            }
        }
    }

    // Part D: the branch becomes REAL control flow (a Stmt-level IfThenElse
    // with a store per arm), not an if_then_else expression in a single store.
    {
        Func f("real_cf");
        f(x) = branch(x > 3, x * 2, x + 1);
        Module m = f.compile_to_module({}, "real_cf");

        HasRealBranch hb;
        if (!scan_module(m, hb)) {
            printf("branch did not become a Stmt-level IfThenElse\n");
            return 1;
        }
    }

    // Part E: when the condition depends on the innermost var, the branch sits
    // inside that loop, so there is no loop level inside the branch to
    // compute_at onto. The producer is therefore NOT gated here (that is
    // inherent, not a bug) - we only check the result is still correct.
    {
        Func p("gated"), f("uses_gated");
        p(x) = x * x + 7;
        f(x) = branch(x < 128, p(x), x + 1);
        p.compute_at(f, x);

        Buffer<int> r = f.realize({256});
        for (int i = 0; i < r.width(); i++) {
            int correct = i < 128 ? (i * i + 7) : (i + 1);
            if (r(i) != correct) {
                printf("producer mismatch at %d: got %d want %d\n", i, r(i), correct);
                return 1;
            }
        }
    }

    // Part F: the condition is hoisted out of loops it does not depend on, so a
    // producer can be compute_at'd at an inner level inside the hoisted branch.
    {
        Func p("hoist_p"), f("hoist_f");
        p(x, y) = x + y;
        f(x, y) = branch(y < 8, p(x, y), x - y);  // condition uses only y
        p.compute_at(f, x);

        Module m = f.compile_to_module({}, "hoist_f");
        ProduceInsideBranch pib("hoist_p");
        if (!scan_module(m, pib)) {
            printf("branch was not hoisted / producer not gated\n");
            return 1;
        }

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? (i + j) : (i - j);
                if (r(i, j) != correct) {
                    printf("hoist mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part G: branch() works inside a GPU kernel (a real per-thread branch).
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

    // Part L: a multi-way branch becomes a real if / else-if / else chain, not
    // just a correct value.
    {
        Func f("multi_cf");
        f(x) = branch(x < 10, 1, x < 20, 2, 3);
        Module m = f.compile_to_module({}, "multi_cf");
        if (count_real_branches(m) < 2) {
            printf("multi-way branch did not become a chain of real branches\n");
            return 1;
        }
    }

    // Part M: the branch is hoisted out of loops its condition does not use,
    // and is NOT hoisted when the condition uses the innermost loop var.
    {
        Func fo("hoist_outer");
        fo(x, y) = branch(y < 8, x + y, x - y);  // condition uses only y
        Module mo = fo.compile_to_module({}, "hoist_outer");
        BranchAboveLoop above;
        if (!scan_module(mo, above)) {
            printf("branch was not hoisted above the inner loop\n");
            return 1;
        }

        Func fi("hoist_inner");
        fi(x, y) = branch(x < 8, x + y, x - y);  // condition uses innermost var
        Module mi = fi.compile_to_module({}, "hoist_inner");
        BranchAboveLoop above2;
        if (scan_module(mi, above2)) {
            printf("branch was hoisted out of a loop its condition uses\n");
            return 1;
        }
    }

    // Part N: a compute_root producer is NOT gated by the branch (its produce
    // loop sits outside). This is the inherent bounds limitation, not a bug.
    {
        Func p("root_p"), f("root_f");
        p(x) = x * x;
        f(x) = branch(x < 128, p(x), x + 1);
        p.compute_root();

        Module m = f.compile_to_module({}, "root_f");
        ProduceInsideBranch pib("root_p");
        if (scan_module(m, pib)) {
            printf("a compute_root producer should not be inside the branch\n");
            return 1;
        }

        Buffer<int> r = f.realize({256});
        for (int i = 0; i < r.width(); i++) {
            int correct = i < 128 ? (i * i) : (i + 1);
            if (r(i) != correct) {
                printf("compute_root mismatch at %d\n", i);
                return 1;
            }
        }
    }

    // Part O: a branch works as an update definition over an RDom, updating
    // only part of the domain and leaving the rest from the pure definition.
    {
        Func f("upd_branch");
        f(x) = -1;
        RDom r(0, 6);
        f(r) = branch(r < 3, 100, 200);
        Buffer<int> res = f.realize({10});
        for (int i = 0; i < res.width(); i++) {
            int correct = (i < 3) ? 100 : (i < 6 ? 200 : -1);
            if (res(i) != correct) {
                printf("update-stage branch mismatch at %d: got %d want %d\n",
                       i, res(i), correct);
                return 1;
            }
        }
    }

    // Part P: branch composes with specialize (both make control flow).
    {
        Param<bool> sp("sp");
        Func f("spec_branch");
        f(x) = branch(x < 5, x * 2, x + 1);
        f.specialize(sp);

        for (bool v : {true, false}) {
            sp.set(v);
            Buffer<int> r = f.realize({10});
            for (int i = 0; i < r.width(); i++) {
                int correct = i < 5 ? (i * 2) : (i + 1);
                if (r(i) != correct) {
                    printf("specialize+branch mismatch at %d (sp=%d)\n", i, (int)v);
                    return 1;
                }
            }
        }
    }

    // Part Q: branch on a non-integer type.
    {
        Func f("float_branch");
        f(x) = branch(x < 5, cast<float>(x) * 2.5f, cast<float>(x) + 1.5f);
        Buffer<float> r = f.realize({10});
        for (int i = 0; i < r.width(); i++) {
            float correct = i < 5 ? (i * 2.5f) : (i + 1.5f);
            if (std::abs(r(i) - correct) > 1e-5f) {
                printf("float branch mismatch at %d: got %f want %f\n",
                       i, r(i), correct);
                return 1;
            }
        }
    }

    // Part R: a producer used by BOTH arms, compute_at'd inside the hoisted
    // branch, is computed in whichever side runs.
    {
        Func p("shared_p"), f("shared_f");
        p(x, y) = x + y;
        f(x, y) = branch(y < 8, p(x, y) * 2, p(x, y) + 1);
        p.compute_at(f, x);

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? ((i + j) * 2) : ((i + j) + 1);
                if (r(i, j) != correct) {
                    printf("shared-producer mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part S: branch inside a parallel loop.
    {
        Func f("par_branch");
        f(x, y) = branch(y < 8, x + y, x - y);
        f.parallel(y);
        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? (i + j) : (i - j);
                if (r(i, j) != correct) {
                    printf("parallel branch mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part T: SELECTIVE gating. Each arm uses a different producer; with both
    // compute_at'd inside the hoisted branch, each producer should only be
    // computed in the arm that actually uses it.
    {
        Func g("sel_g"), h("sel_h"), f("selective");
        g(x, y) = x + y;
        h(x, y) = x * y;
        f(x, y) = branch(y < 8, g(x, y), h(x, y));
        g.compute_at(f, x);
        h.compute_at(f, x);

        Module m = f.compile_to_module({}, "selective");
        BranchArmProduce pg = arm_produce(m, "sel_g");
        BranchArmProduce ph = arm_produce(m, "sel_h");
        if (!pg.found) {
            printf("selective: no real branch found\n");
            return 1;
        }
        if (pg.then_count < 1 || pg.else_count != 0) {
            printf("selective: sel_g should be produced only in the then arm "
                   "(then=%d else=%d)\n",
                   pg.then_count, pg.else_count);
            return 1;
        }
        if (ph.else_count < 1 || ph.then_count != 0) {
            printf("selective: sel_h should be produced only in the else arm "
                   "(then=%d else=%d)\n",
                   ph.then_count, ph.else_count);
            return 1;
        }

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? (i + j) : (i * j);
                if (r(i, j) != correct) {
                    printf("selective mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part U: a fully loop-invariant condition (a Param) hoists above every
    // loop, so the whole loop nest sits inside the branch.
    {
        Param<int> q("q");
        Func f("invariant");
        f(x, y) = branch(q > 0, x + y, x - y);

        Module m = f.compile_to_module({q}, "invariant");
        BranchInsideAnyLoop inside;
        if (scan_module(m, inside)) {
            printf("a loop-invariant branch was not hoisted above all loops\n");
            return 1;
        }

        q.set(1);
        Buffer<int> r = f.realize({8, 8});
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                if (r(i, j) != i + j) {
                    printf("invariant branch mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part V: a multi-way branch whose conditions live at DIFFERENT loop levels.
    // The outer condition (on y) can hoist out of the x loop, but the inner one
    // (on x) must not be dragged out with it.
    {
        Func f("mixed_levels");
        f(x, y) = branch(y < 8, 1, x < 4, 2, 3);
        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? 1 : (i < 4 ? 2 : 3);
                if (r(i, j) != correct) {
                    printf("mixed-level mismatch at (%d,%d): got %d want %d\n",
                           i, j, r(i, j), correct);
                    return 1;
                }
            }
        }
    }

    // Part W: a branch-defined Func used as a compute_root intermediate (it can
    // not be inlined, so it must be scheduled).
    {
        Func b("mid_branch"), f("mid_consumer");
        b(x) = branch(x < 5, x * 2, x + 1);
        b.compute_root();
        f(x) = b(x) + 100;

        Buffer<int> r = f.realize({10});
        for (int i = 0; i < r.width(); i++) {
            int correct = (i < 5 ? i * 2 : i + 1) + 100;
            if (r(i) != correct) {
                printf("intermediate branch mismatch at %d\n", i);
                return 1;
            }
        }
    }

    // Part X: chained branch Funcs (a branch consuming another branch).
    {
        Func b1("chain1"), b2("chain2"), f("chain_out");
        b1(x) = branch(x < 5, x * 2, x + 1);
        b1.compute_root();
        b2(x) = branch(x % 2 == 0, b1(x), 0);
        b2.compute_root();
        f(x) = b2(x);

        Buffer<int> r = f.realize({10});
        for (int i = 0; i < r.width(); i++) {
            int inner = (i < 5) ? i * 2 : i + 1;
            int correct = (i % 2 == 0) ? inner : 0;
            if (r(i) != correct) {
                printf("chained branch mismatch at %d: got %d want %d\n",
                       i, r(i), correct);
                return 1;
            }
        }
    }

    // Part Y: branch survives tiling (the condition ends up in terms of the
    // split vars).
    {
        Func f("tiled");
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        f(x, y) = branch(y < 8, x + y, x - y);
        f.tile(x, y, xo, yo, xi, yi, 4, 4);

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? (i + j) : (i - j);
                if (r(i, j) != correct) {
                    printf("tiled branch mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part Z: branch survives reorder (the condition var becomes the inner loop).
    {
        Func f("reordered");
        f(x, y) = branch(y < 8, x + y, x - y);
        f.reorder(y, x);  // y is now the inner loop -> can not hoist out of it

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? (i + j) : (i - j);
                if (r(i, j) != correct) {
                    printf("reordered branch mismatch at (%d,%d)\n", i, j);
                    return 1;
                }
            }
        }
    }

    // Part AA: a stencil producer (needs a window) compute_at'd inside a hoisted
    // branch still gets correct bounds.
    {
        Func p("stencil_p"), f("stencil_f");
        p(x, y) = x + y;
        f(x, y) = branch(y < 8, p(x - 1, y) + p(x + 1, y), 0);
        p.compute_at(f, x);

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = (j < 8) ? ((i - 1 + j) + (i + 1 + j)) : 0;
                if (r(i, j) != correct) {
                    printf("stencil mismatch at (%d,%d): got %d want %d\n",
                           i, j, r(i, j), correct);
                    return 1;
                }
            }
        }
    }

    // Part AB: branch survives unrolling.
    {
        Func f("unrolled");
        Var xo("xo"), xi("xi");
        f(x) = branch(x < 5, x * 2, x + 1);
        f.split(x, xo, xi, 4).unroll(xi);

        Buffer<int> r = f.realize({16});
        for (int i = 0; i < r.width(); i++) {
            int correct = i < 5 ? i * 2 : i + 1;
            if (r(i) != correct) {
                printf("unrolled branch mismatch at %d\n", i);
                return 1;
            }
        }
    }

    // Part AC: branch in an atomic update. The atomic node wraps the branch, so
    // the branch is not hoisted there, but it must still be correct.
    {
        Func f("atomic_branch");
        f(x) = 0;
        RDom r(0, 10);
        f(r) = branch(r < 5, 100, 200);
        f.update(0).atomic(true);

        Buffer<int> res = f.realize({10});
        for (int i = 0; i < res.width(); i++) {
            int correct = i < 5 ? 100 : 200;
            if (res(i) != correct) {
                printf("atomic branch mismatch at %d: got %d want %d\n",
                       i, res(i), correct);
                return 1;
            }
        }
    }

    // Part AD: branch composes with fused loop nests (compute_with).
    {
        Func fa("cw_a"), fb("cw_b"), out("cw_out");
        fa(x, y) = branch(y < 8, x + y, x - y);
        fb(x, y) = x * y;
        out(x, y) = fa(x, y) + fb(x, y);
        fa.compute_root();
        fb.compute_root();
        fb.compute_with(fa, y);

        Buffer<int> r = out.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = ((j < 8) ? (i + j) : (i - j)) + i * j;
                if (r(i, j) != correct) {
                    printf("compute_with branch mismatch at (%d,%d): got %d want %d\n",
                           i, j, r(i, j), correct);
                    return 1;
                }
            }
        }
    }

    // Part AF: reverse-mode (VJP) autodiff, the default Halide autodiff. The
    // gradient through a branch must match the gradient through the equivalent
    // select.
    {
        Param<float> p("pv");
        p.set(3.0f);
        Var xx("xx");
        Expr cond = xx < 5;
        Expr a = p * cast<float>(xx);
        Expr b = p * p;
        Func fs("vjp_sel"), fb("vjp_br");
        fs(xx) = select(cond, a, b);
        fb(xx) = branch(cond, a, b);
        fs.compute_root();
        fb.compute_root();  // branch must be a real Func, not inlined into loss
        RDom r(0, 10);
        Func loss_s("loss_s"), loss_b("loss_b");
        loss_s() = sum(fs(r));
        loss_b() = sum(fb(r));
        Func gs = propagate_adjoints(loss_s)(p);
        Func gb = propagate_adjoints(loss_b)(p);
        Buffer<float> bs = gs.realize();
        Buffer<float> bb = gb.realize();
        if (std::abs(bs() - bb()) > 1e-3f) {
            printf("VJP gradient mismatch: select=%f branch=%f\n", bs(), bb());
            return 1;
        }
    }

    // Part AH: f(x) += branch(cond, a, b) distributes the accumulation into the
    // arms - f = branch(cond, f + a, f + b) - so the whole update value is a
    // branch (real control flow), not a select. A producer used by one arm and
    // compute_at'd inside the update is gated to that arm only.
    {
        Func p("acc_p"), f("acc_f");
        p(x, y) = x + y;  // stand-in for an expensive producer
        f(x, y) = 100;
        f(x, y) += branch(y < 8, p(x, y), x - y);
        p.compute_at(f, x);  // lands inside the branched update

        Module m = f.compile_to_module({}, "acc_f");
        if (count_real_branches(m) < 1) {
            printf("f += branch did not lower to a real branch\n");
            return 1;
        }
        BranchArmProduce pg = arm_produce(m, "acc_p");
        if (!pg.found) {
            printf("f += branch: no branch found around producer\n");
            return 1;
        }
        if (pg.then_count < 1 || pg.else_count != 0) {
            printf("f += branch: producer not gated to the taken arm (then=%d else=%d)\n",
                   pg.then_count, pg.else_count);
            return 1;
        }

        Buffer<int> r = f.realize({16, 16});
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int correct = 100 + ((j < 8) ? (i + j) : (i - j));
                if (r(i, j) != correct) {
                    printf("f += branch mismatch at (%d,%d): got %d want %d\n",
                           i, j, r(i, j), correct);
                    return 1;
                }
            }
        }
    }

    // Part AH2: reverse-mode (VJP) through a branch with a NONLINEAR loss. The
    // backward pass needs the forward value of the branch-valued Func, which used
    // to bury the branch inside an adjoint expression and fail lowering with
    // "a branch-defined Func can not be inlined". It now lifts back to real
    // control flow, and the gradient matches both select and the analytic value.
    {
        auto grad = [&](bool use_branch) -> float {
            Param<float> p("pn");
            p.set(3.0f);
            Var xx("xx");
            Expr cond = xx < 5;
            Expr a = p * cast<float>(xx);  // da/dp = xx
            Expr b = p * p;                // db/dp = 2p
            Func fwd(use_branch ? "vjpn_b" : "vjpn_s");
            if (use_branch) {
                fwd(xx) = branch(cond, a, b);
            } else {
                fwd(xx) = select(cond, a, b);
            }
            fwd.compute_root();
            RDom r(0, 10);
            Func loss(use_branch ? "lossn_b" : "lossn_s");
            loss() = sum(fwd(r) * fwd(r));  // nonlinear in fwd
            loss.compute_root();
            Buffer<float> g = propagate_adjoints(loss)(p).realize();
            return g();
        };
        float gs = grad(false), gb = grad(true);
        // sum_{xx<5} 2 p xx^2 + sum_{xx>=5} 4 p^3 = 180 + 540 = 720 at p=3.
        if (std::abs(gs - 720.0f) > 1e-2f || std::abs(gb - gs) > 1e-2f) {
            printf("nonlinear VJP mismatch: select=%f branch=%f analytic=720\n", gs, gb);
            return 1;
        }
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    // Part H: a lane-varying (vector) condition is rejected at construction.
    {
        bool threw = false;
        try {
            (void)branch(Broadcast::make(x > 5, 4), x * 2, x + 1);
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch() with a vector condition should have thrown\n");
            return 1;
        }
    }

    // Part I: a condition that depends on a vectorized dimension is rejected.
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
            printf("branch() on a vectorized dimension should have thrown\n");
            return 1;
        }
    }

    // Part J: branch() combined with vectorization inside a GPU kernel errors.
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
            printf("branch() with vectorization in a GPU kernel should have thrown\n");
            return 1;
        }
    }

    // Part AE: a branch produces a single value, so it can not define a stage of
    // a tuple-valued Func. That must be a clean error, not a crash.
    {
        bool threw = false;
        try {
            Func f("tuple_branch");
            f(x) = Tuple(0, 1);                // two values
            f(x) = branch(x < 5, 100, 200);    // branch gives one -> mismatch
            f.realize({10});
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("branch on a tuple-valued Func should have thrown\n");
            return 1;
        }
    }

    // Part K: a branch-defined Func can not be inlined (the branch must be the
    // whole value of a definition), so using one inline is an error.
    {
        Func b("inlined_branch"), f("consumer");
        b(x) = branch(x < 5, x * 2, x + 1);
        f(x) = b(x) + 1;  // b is inlined by default -> branch gets buried
        bool threw = false;
        try {
            f.compile_to_module({}, "consumer");
        } catch (const CompileError &) {
            threw = true;
        }
        if (!threw) {
            printf("inlining a branch-defined Func should have thrown\n");
            return 1;
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
