#include "CheckGPUCrossTalk.h"

#include "CSE.h"
#include "CanonicalizeGPUVars.h"
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
};

class FindAccesses : public IRVisitor {
    using IRVisitor::visit;

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
            IRVisitor::visit(op);
        }
    }

    void visit(const LetStmt *op) override {
        if (!thread_loops.empty()) {
            per_thread.insert(op->name);
        }
        IRVisitor::visit(op);
    }

    void visit(const Let *op) override {
        if (!thread_loops.empty()) {
            per_thread.insert(op->name);
        }
        IRVisitor::visit(op);
    }

    void visit(const Provide *op) override {
        if (op->name == func) {
            accesses.push_back({op->args, thread_loops, true});
        }
        IRVisitor::visit(op);
    }

    void visit(const Call *op) override {
        if (op->name == func && op->call_type == Call::Halide) {
            accesses.push_back({op->args, thread_loops, false});
        }
        IRVisitor::visit(op);
    }

    const string &func;
    vector<ThreadLoop> thread_loops;

public:
    vector<Access> accesses;
    // Names bound at or inside the loops over threads, so they may take a
    // different value in a different thread. Everything else, such as the base
    // of the block's tile, is shared by the whole block.
    std::set<string> per_thread;

    FindAccesses(const string &func)
        : func(func) {
    }
};

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

// Rename only the variables that can differ between two threads of a block.
// Renaming the rest would describe a thread of some other block, which is not
// the question being asked.
class RenamePerThreadVars : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        if (names.count(op->name)) {
            return Variable::make(op->type, op->name + "$_");
        }
        return op;
    }

    const std::set<string> &names;

public:
    using IRMutator::mutate;

    RenamePerThreadVars(const std::set<string> &names)
        : names(names) {
    }
};

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

        // Make an access by some other thread, in the manner of
        // can_parallelize_rvar, and try to prove the two threads can never
        // meet at the same site. Comparing the arguments one dimension at a
        // time is what makes this provable. Anything the simplifier cannot see
        // through, such as the data-dependent argument of a scatter, just
        // means we fail to prove it and reject the schedule.
        std::set<string> per_thread = finder.per_thread;
        for (int i = 0; i < 3; i++) {
            per_thread.insert(gpu_thread_name(i));
        }
        RenamePerThreadVars renamer(per_thread);
        Expr distinct = const_false();
        Scope<Interval> bounds;
        for (int i = 0; i < thread_dims; i++) {
            const string &name = gpu_thread_name(i);
            Expr me = Variable::make(Int(32), name);
            Expr them = Variable::make(Int(32), name + "$_");
            distinct = distinct || (me != them);
            Expr extent;
            for (const Access &a : finder.accesses) {
                size_t n = a.thread_loops.size();
                if ((int)n > i) {
                    const Expr &e = a.thread_loops[n - 1 - i].extent;
                    extent = extent.defined() ? simplify(max(extent, e)) : e;
                }
            }
            if (extent.defined() && is_const(extent)) {
                Interval in(0, simplify(extent - 1));
                bounds.push(name, in);
                bounds.push(name + "$_", in);
            }
        }

        // A thread may only touch what it stores itself, so look for a meeting
        // between any access and some other thread's store.
        Expr hazard = const_false();
        for (const Access &a : finder.accesses) {
            vector<Expr> mine = canonical(a);
            for (const Access &b : finder.accesses) {
                if (!b.is_store) {
                    continue;
                }
                vector<Expr> theirs = canonical(b);
                if (mine.size() != theirs.size()) {
                    continue;
                }
                Expr meet = const_true();
                for (size_t i = 0; i < mine.size(); i++) {
                    meet = meet && (mine[i] == renamer.mutate(theirs[i]));
                }
                hazard = hazard || (distinct && meet);
            }
        }

        hazard = common_subexpression_elimination(hazard);
        hazard = substitute_in_boolean_lets(hazard);
        hazard = simplify(hazard, bounds);

        if (!is_const_zero(hazard)) {
            std::ostringstream accessed;
            for (const Access &a : finder.accesses) {
                accessed << "  " << op->name << "(";
                for (size_t i = 0; i < a.args.size(); i++) {
                    accessed << (i ? ", " : "") << a.args[i];
                }
                accessed << ")" << (a.is_store ? " (stored)" : " (loaded)") << "\n";
            }
            user_error
                << "The allocation " << op->name << " is scheduled to live in "
                << op->memory_type << " memory, which is private to a GPU thread, but it is "
                << "scheduled outside the loops over GPU threads, so every thread gets its "
                << "own copy of it rather than sharing one. Halide could not prove that each "
                << "thread keeps to its own part of it, so a thread may be relying on a value "
                << "another thread was responsible for, which it does not have. It is "
                << "accessed at:\n"
                << accessed.str()
                << "Either schedule " << op->name << " inside the loops over GPU threads, or "
                << "store it in GPUShared memory, which the threads of a block do share.\n";
        }
    }
};

}  // namespace

void check_gpu_cross_talk(const Stmt &s) {
    CheckCrossTalk checker;
    s.accept(&checker);
}

}  // namespace Internal
}  // namespace Halide
