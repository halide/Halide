#include "CheckGPUCrossTalk.h"

#include "Bounds.h"
#include "CanonicalizeGPUVars.h"
#include "ExprUsesVar.h"
#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Substitute.h"

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

// An access to the allocation being checked, by dimension. Accesses are
// recorded in the order they appear, so an earlier one in the list is one that
// has already happened.
struct Access {
    // As written, for error messages. Left in terms of whatever variables the
    // schedule named, which is what the user will recognize.
    vector<Expr> args;
    // In terms of the loops the fused loops over threads will use, so that two
    // accesses can be compared.
    vector<Expr> canonical_args;
    // How many loops over threads this sits in.
    int thread_depth;
    bool is_store;
};

class FindAccesses : public IRVisitor {
    using IRVisitor::visit;

    // Rewrite an expr into the terms two accesses can be compared in. An index
    // is usually written using let-bound variables, and the producer and the
    // consumer name theirs differently, so put the definitions back around it.
    // Then swap each loop over threads for the fused loop it will become:
    // counting inwards, the nth loop around the expr is the nth thread
    // dimension, whatever it is called, and its min is folded in, because the
    // fused loop starts at zero.
    Expr canonicalize(const Expr &e) const {
        Expr result = rewrap_used_lets(e, lets);
        for (size_t i = 0; i < thread_loops.size() && i < 3; i++) {
            const ThreadLoop &t = thread_loops[thread_loops.size() - 1 - i];
            Expr v = Variable::make(Int(32), gpu_thread_name((int)i)) + t.min;
            result = simplify(substitute(t.name, v, result));
        }
        return result;
    }

    void visit(const For *op) override {
        // Only a loop over threads separates one thread's copy of the
        // allocation from another's. A loop over lanes does not: the lanes of a
        // warp can reach each other's registers, which is what LowerWarpShuffles
        // is for, so one is just another loop a single thread runs.
        if (op->for_type == ForType::GPUThread) {
            thread_loops.push_back({op->name, op->min, op->extent()});
            IRVisitor::visit(op);
            thread_loops.pop_back();
        } else {
            // The loops a thread runs inside its own part of the allocation
            // bound how far its accesses reach, which is what says the parts
            // do not overlap. A loop that starts at the thread's own part of
            // the allocation has bounds that speak of the thread, so they get
            // the same treatment as the accesses themselves.
            Expr min = canonicalize(op->min), max = canonicalize(op->max);
            loop_bounds.emplace_back(op->name, Interval(min, max));
            IRVisitor::visit(op);
        }
    }

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        lets.emplace_back(op->name, op->value);
        op->body.accept(this);
        lets.pop_back();
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        lets.emplace_back(op->name, op->value);
        op->body.accept(this);
        lets.pop_back();
    }

    // Record an access, in terms of both the loops it was written with and the
    // loops over threads it will end up in.
    void record(const vector<Expr> &args, bool is_store) {
        vector<Expr> canonical;
        canonical.reserve(args.size());
        for (const Expr &e : args) {
            canonical.push_back(canonicalize(e));
        }
        // The nth loop counting inwards from this access is the nth thread
        // dimension, so that is where its extent belongs.
        int depth = (int)thread_loops.size();
        for (int i = 0; i < depth && i < 3; i++) {
            const Expr &e = thread_loops[depth - 1 - i].extent;
            thread_extents[i] = thread_extents[i].defined() ?
                                    simplify(max(thread_extents[i], e)) :
                                    e;
        }
        accesses.push_back({args, canonical, depth, is_store});
    }

    void visit(const Provide *op) override {
        // The values are read before the store happens, which matters for an
        // update definition, where the value reads the site being stored to.
        IRVisitor::visit(op);
        if (op->name == func) {
            record(op->args, true);
        }
    }

    void visit(const Call *op) override {
        if (op->name == func && op->call_type == Call::Halide) {
            record(op->args, false);
        }
        IRVisitor::visit(op);
    }

    const string &func;
    vector<ThreadLoop> thread_loops;
    vector<std::pair<string, Expr>> lets;

public:
    vector<Access> accesses;
    vector<std::pair<string, Interval>> loop_bounds;
    // The most threads there are in each dimension, if that is known.
    Expr thread_extents[3];

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

class CheckCrossTalk : public IRVisitor {
    using IRVisitor::visit;

    bool in_threads = false;

    void visit(const For *op) override {
        // An allocation inside a loop over threads or lanes already belongs to
        // whoever runs that loop, so there is nothing to tell apart. Note that
        // a lane loop counts here but not when deciding which loops separate
        // one thread's copy from another's, because a warp shares its lanes'
        // registers but two threads share nothing.
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
            thread_dims = std::max(thread_dims, std::min(a.thread_depth, 3));
            internal_assert(a.args.size() == finder.accesses[0].args.size())
                << "Accesses to " << op->name << " disagree about how many "
                << "dimensions it has\n";
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
            const Expr &extent = finder.thread_extents[i];
            if (extent.defined() && is_const(extent)) {
                thread_bounds.push(gpu_thread_name(i), Interval(0, simplify(extent - 1)));
            }
        }

        // The region each access touches, by dimension. A tail strategy wraps
        // the clamp on the last thread's part in a likely intrinsic, which
        // stops the simplifier folding it away once the number of threads is
        // known.
        vector<vector<Interval>> regions(finder.accesses.size());
        for (size_t i = 0; i < finder.accesses.size(); i++) {
            for (const Expr &arg : finder.accesses[i].canonical_args) {
                Expr e = simplify(remove_likelies(arg), thread_bounds);
                regions[i].push_back(bounds_of_expr_in_scope(e, bounds));
            }
        }

        // Which dimensions tell one thread's part from another's. A
        // dimension whose region is the same whatever thread is asking is one
        // every thread walks the same way, so a coordinate a load names in it
        // is one every thread names. Such a dimension cannot be what makes a
        // load another thread's. It is the region that has to be asked, not
        // the index: a loop of a thread's own is written the same way by every
        // thread, and it is its bounds that say which part is whose.
        // A dimension nothing could be bounded in stays in, so that failing
        // to work out where an access reaches is still an error rather than a
        // dimension that gets to sit the comparison out.
        vector<bool> separates_threads(finder.accesses[0].args.size(), false);
        for (const auto &region : regions) {
            for (size_t i = 0; i < region.size(); i++) {
                if (!region[i].has_lower_bound() || !region[i].has_upper_bound()) {
                    separates_threads[i] = true;
                    continue;
                }
                for (int t = 0; t < 3; t++) {
                    const string &n = gpu_thread_name(t);
                    separates_threads[i] =
                        separates_threads[i] ||
                        stmt_or_expr_uses_var(region[i].min, n) ||
                        stmt_or_expr_uses_var(region[i].max, n);
                }
            }
        }

        // Does store s reach every site load l does, along the dimensions
        // that tell one thread's part from another's?
        const auto covers = [&](size_t s, size_t l) {
            for (size_t i = 0; i < regions[l].size(); i++) {
                if (!separates_threads[i]) {
                    continue;
                }
                const Interval &want = regions[l][i], &have = regions[s][i];
                if (!(want.has_lower_bound() && want.has_upper_bound() &&
                      have.has_lower_bound() && have.has_upper_bound() &&
                      can_prove(have.min <= want.min && want.max <= have.max))) {
                    return false;
                }
            }
            return true;
        };

        for (size_t l = 0; l < finder.accesses.size(); l++) {
            const Access &load = finder.accesses[l];
            if (load.is_store) {
                continue;
            }
            bool ok = false;
            // Stores later in the list have not happened yet. Ones earlier
            // have, except across the arms of an if, which doesn't matter here:
            // any value the output depends on was stored by some thread, so if
            // no other thread stored this one, this thread did. A site this
            // thread never wrote holds a value nothing depends on, like the
            // garbage that pads out a vector.
            // Stores earlier in the list have happened. A later one still
            // counts if it lands somewhere else along a dimension that does
            // not separate threads, because such a dimension is what carries
            // an allocation from one run of a loop to the next: the store this
            // load wants is the one the previous iteration ran, and the thread
            // that ran it was this one. Where every dimension agrees there is
            // no such gap, and a later store is simply later.
            for (size_t s = 0; s < finder.accesses.size() && !ok; s++) {
                const Access &store = finder.accesses[s];
                // The store has to be in at least as many loops over threads,
                // or it is the work of one thread standing in for all of them.
                if (!store.is_store || store.thread_depth < load.thread_depth) {
                    continue;
                }
                if (s > l) {
                    bool carried = false;
                    for (size_t i = 0; i < regions[l].size() && !carried; i++) {
                        carried = (!separates_threads[i] &&
                                   !equal(load.canonical_args[i], store.canonical_args[i]));
                    }
                    if (!carried) {
                        continue;
                    }
                }
                ok = covers(s, l);
            }
            // Finding one store of this thread's that covers the load is
            // not enough. What a thread reads is what was written to the site
            // last, so any store that might land on the same site has to be
            // this thread's too. A store that cannot reach the site is no
            // one's business, which is what the overlap test asks.
            //
            // Two ways a store that reaches it belongs to someone else: it
            // runs in fewer loops over threads than the load, so one thread
            // ran it on everyone's behalf; or it reaches the site from a
            // different thread, which is what failing to cover the load along
            // the dimensions that separate threads means.
            for (size_t s = 0; s < finder.accesses.size() && ok; s++) {
                const Access &store = finder.accesses[s];
                if (!store.is_store) {
                    continue;
                }
                if (store.thread_depth >= load.thread_depth && covers(s, l)) {
                    continue;
                }
                bool disjoint = false;
                for (size_t i = 0; i < regions[l].size() && !disjoint; i++) {
                    const Interval &a = regions[l][i], &b = regions[s][i];
                    disjoint = ((a.has_upper_bound() && b.has_lower_bound() &&
                                 can_prove(a.max < b.min)) ||
                                (b.has_upper_bound() && a.has_lower_bound() &&
                                 can_prove(b.max < a.min)));
                }
                ok = disjoint;
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
