#include "CheckGPUCrossTalk.h"

#include "Bounds.h"
#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "ExprUsesVar.h"
#include "IR.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Substitute.h"

#include <set>
#include <sstream>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

// One loop over GPU threads. The producer and the consumer of an allocation
// have their own loops over the same threads, which may not even have the same
// bounds, so an access is only meaningful alongside the loops around it.
struct ThreadLoop {
    string name;
    Expr min, extent;
};

// An access to the allocation being checked, by dimension, and the loops over
// threads it sits in, outermost first.
struct Access {
    vector<Expr> args;
    vector<ThreadLoop> thread_loops;
    bool is_store;
    // Where this sits in the body, so we can tell a store that has already
    // happened from one that has not happened yet.
    int order;
};

class FindAccesses : public IRVisitor {
    using IRVisitor::visit;

    // An index is usually in terms of let-bound variables, and the producer
    // and the consumer name theirs differently, so put them back.
    Expr resolve(Expr e) const {
        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            if (expr_uses_var(e, it->first)) {
                e = substitute(it->first, it->second, e);
            }
        }
        return e;
    }

    vector<Expr> resolve(const vector<Expr> &args) const {
        vector<Expr> result;
        result.reserve(args.size());
        for (const Expr &e : args) {
            result.push_back(resolve(e));
        }
        return result;
    }

    void visit(const For *op) override {
        if (op->for_type == ForType::GPUThread) {
            thread_loops.push_back({op->name, op->min, op->extent()});
            per_thread.insert(op->name);
            IRVisitor::visit(op);
            thread_loops.pop_back();
        } else {
            if (!thread_loops.empty()) {
                per_thread.insert(op->name);
            }
            // The loops a thread runs inside its own part of the allocation
            // bound how far its accesses reach, which is what says the parts
            // do not overlap.
            Expr min = resolve(op->min), max = resolve(op->max);
            if (is_const(min) && is_const(max)) {
                loop_bounds.emplace_back(op->name, Interval(min, max));
            }
            IRVisitor::visit(op);
        }
    }

    void visit(const LetStmt *op) override {
        if (!thread_loops.empty()) {
            per_thread.insert(op->name);
        }
        op->value.accept(this);
        lets.emplace_back(op->name, op->value);
        op->body.accept(this);
        lets.pop_back();
    }

    void visit(const Let *op) override {
        if (!thread_loops.empty()) {
            per_thread.insert(op->name);
        }
        op->value.accept(this);
        lets.emplace_back(op->name, op->value);
        op->body.accept(this);
        lets.pop_back();
    }

    void visit(const Provide *op) override {
        // The values are read before the store happens, which matters for an
        // update definition, where the value reads the site being stored to.
        IRVisitor::visit(op);
        if (op->name == func) {
            accesses.push_back({resolve(op->args), thread_loops, true, order++});
        }
    }

    void visit(const Call *op) override {
        if (op->name == func && op->call_type == Call::Halide) {
            accesses.push_back({resolve(op->args), thread_loops, false, order++});
        }
        IRVisitor::visit(op);
    }

    const string &func;
    vector<ThreadLoop> thread_loops;
    vector<std::pair<string, Expr>> lets;
    int order = 0;

public:
    vector<Access> accesses;
    // Names bound at or inside the loops over threads, so they may take a
    // different value in a different thread. Everything else, such as the base
    // of the block's tile, is shared by the whole block.
    std::set<string> per_thread;
    vector<std::pair<string, Interval>> loop_bounds;

    FindAccesses(const string &func)
        : func(func) {
    }
};

string name_and_args(const string &name, const vector<Expr> &args) {
    std::ostringstream s;
    s << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        s << (i ? ", " : "") << args[i];
    }
    s << ")";
    return s.str();
}

// Rewrite an access in terms of the loops the fused loops over threads will
// use. Counting inwards, the nth loop around an access is the nth thread
// dimension, whatever it is called, and its min is folded in, because the
// fused loop starts at zero.
vector<Expr> canonical(const Access &a) {
    vector<Expr> args = a.args;
    for (size_t i = 0; i < a.thread_loops.size() && i < 3; i++) {
        const ThreadLoop &t = a.thread_loops[a.thread_loops.size() - 1 - i];
        Expr v = Variable::make(Int(32), gpu_thread_name((int)i)) + t.min;
        for (Expr &arg : args) {
            arg = simplify(substitute(t.name, v, arg));
        }
    }
    return args;
}

class CheckCrossTalk : public IRVisitor {
    using IRVisitor::visit;

    bool in_threads = false;

    void visit(const For *op) override {
        if (op->for_type == ForType::GPUThread || op->for_type == ForType::GPULane) {
            ScopedValue<bool> bind(in_threads, true);
            IRVisitor::visit(op);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Realize *op) override {
        // Only memory that is private to a thread, and only when the
        // allocation is outside the loops over threads. An allocation with an
        // automatic memory type that lands outside them goes to shared memory,
        // which the threads of a block really do share.
        if (!in_threads &&
            (op->memory_type == MemoryType::Register ||
             op->memory_type == MemoryType::Stack)) {
            check(op);
        }
        IRVisitor::visit(op);
    }

    void check(const Realize *op) {
        FindAccesses finder(op->name);
        op->body.accept(&finder);

        int thread_dims = 0;
        for (const Access &a : finder.accesses) {
            thread_dims = std::max(thread_dims, (int)std::min(a.thread_loops.size(), (size_t)3));
        }
        if (thread_dims == 0) {
            // Only one thread runs this, so there is no one to talk to.
            return;
        }

        // A thread's own copy only holds what that thread put there, so
        // every load has to be of something this same thread already stored.
        // Comparing the arguments one dimension at a time is what makes this
        // provable; the flattened index of the same access would not be.
        //
        // Two threads storing to one site is not itself a problem. A tail
        // strategy makes neighbouring threads recompute the same values, and
        // each still reads what it wrote.
        // The region a thread touches is in terms of the loops it runs
        // inside its own part, so those are bounded. The thread itself is left
        // symbolic: bounding it would widen every region to cover all threads,
        // which is what we are trying to tell apart.
        Scope<Interval> bounds;
        for (const auto &b : finder.loop_bounds) {
            bounds.push(b.first, b.second);
        }
        // How many threads there are is still worth knowing, to settle the
        // clamp a tail strategy puts on the last thread's part. That only
        // simplifies the expression; it does not go into the region.
        Scope<Interval> thread_bounds;
        for (int i = 0; i < thread_dims; i++) {
            Expr extent;
            for (const Access &a : finder.accesses) {
                size_t n = a.thread_loops.size();
                if ((int)n > i) {
                    const Expr &e = a.thread_loops[n - 1 - i].extent;
                    extent = extent.defined() ? simplify(max(extent, e)) : e;
                }
            }
            if (extent.defined() && is_const(extent)) {
                thread_bounds.push(gpu_thread_name(i), Interval(0, simplify(extent - 1)));
            }
        }

        // A tail strategy wraps the clamp on the last thread's part in a
        // likely intrinsic, which stops the simplifier folding it away once
        // the number of threads is known.
        auto region = [&](const Access &a, size_t dim) {
            Expr e = simplify(remove_likelies(canonical(a)[dim]), thread_bounds);
            return bounds_of_expr_in_scope(e, bounds);
        };

        for (const Access &load : finder.accesses) {
            if (load.is_store) {
                continue;
            }
            bool ok = false;
            for (const Access &store : finder.accesses) {
                // The store has to have happened already, and has to be in at
                // least as many loops over threads, or it is the work of one
                // thread standing in for all of them.
                if (!store.is_store ||
                    store.order > load.order ||
                    store.thread_loops.size() < load.thread_loops.size() ||
                    store.args.size() != load.args.size()) {
                    continue;
                }
                bool covers = true;
                for (size_t i = 0; i < load.args.size() && covers; i++) {
                    Interval l = region(load, i), st = region(store, i);
                    covers = (l.has_lower_bound() && l.has_upper_bound() &&
                              st.has_lower_bound() && st.has_upper_bound() &&
                              can_prove(st.min <= l.min && l.max <= st.max));
                }
                ok = ok || covers;
            }
            if (!ok) {
                report(op, finder.accesses, load);
            }
        }
    }

    void report(const Realize *op, const vector<Access> &accesses, const Access &load) {

            std::ostringstream accessed;
            for (const Access &a : accesses) {
                accessed << "  " << name_and_args(op->name, a.args)
                         << (a.is_store ? " (stored)" : " (loaded)") << "\n";
            }
            user_error
                << "The allocation " << op->name << " is scheduled to live in "
                << op->memory_type << " memory, which is private to a GPU thread, but it is "
                << "scheduled outside the loops over GPU threads, so every thread gets its "
                << "own copy of it rather than sharing one. Halide could not prove that each "
                << "thread keeps to its own part of it, so a thread may be relying on a value "
                << "another thread was responsible for, which it does not have. It is "
                << "loaded at:\n  " << name_and_args(op->name, load.args)
                << "\nwhich is not within what this thread stores. It is accessed at:\n"
                << accessed.str()
                << "Either schedule " << op->name << " inside the loops over GPU threads, or "
                << "store it in GPUShared memory, which the threads of a block do share.\n";
    }
};

}  // namespace

void check_gpu_cross_talk(const Stmt &s) {
    CheckCrossTalk checker;
    s.accept(&checker);
}

}  // namespace Internal
}  // namespace Halide
