#include "PromoteGPURegisters.h"

#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "MultiRamp.h"

#include <map>

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

// Every access to the allocation, in the order they appear.
class FindAccesses : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Store *op) override {
        if (op->name == alloc) {
            indices.push_back(op->index);
        }
        IRVisitor::visit(op);
    }

    void visit(const Load *op) override {
        if (op->name == alloc) {
            indices.push_back(op->index);
        }
        IRVisitor::visit(op);
    }

    const string &alloc;

public:
    vector<Expr> indices;

    FindAccesses(const string &alloc)
        : alloc(alloc) {
    }
};

// Which kinds of loop over the threads of a block appear in some IR.
class LoopKinds : public IRVisitor {
    using IRVisitor::visit;

    void visit(const For *op) override {
        threads = threads || op->for_type == ForType::GPUThread;
        lanes = lanes || op->for_type == ForType::GPULane;
        IRVisitor::visit(op);
    }

public:
    bool threads = false, lanes = false;
};

// Replace each access with the one worked out for it below.
class RewriteAccesses : public IRMutator {
public:
    using IRMutator::mutate;

private:
    using IRMutator::visit;

    Expr index_for(const Expr &index) const {
        auto it = rewritten.find(index);
        internal_assert(it != rewritten.end());
        return it->second;
    }

    Stmt visit(const Store *op) override {
        Stmt s = IRMutator::visit(op);
        if (op->name == alloc) {
            op = s.as<Store>();
            s = op->with(op->value, index_for(op->index), op->predicate, ModulusRemainder());
        }
        return s;
    }

    Expr visit(const Load *op) override {
        Expr e = IRMutator::visit(op);
        if (op->name == alloc) {
            op = e.as<Load>();
            e = op->with(index_for(op->index), op->predicate, ModulusRemainder());
        }
        return e;
    }

    const string &alloc;
    const map<Expr, Expr, IRDeepCompare> &rewritten;

public:
    RewriteAccesses(const string &alloc, const map<Expr, Expr, IRDeepCompare> &rewritten)
        : alloc(alloc), rewritten(rewritten) {
    }
};

class PromoteGPURegisters : public IRMutator {
public:
    using IRMutator::mutate;

private:
    using IRMutator::visit;

    bool in_threads = false;
    vector<const Allocate *> pending;

    Stmt visit(const Allocate *op) override {
        LoopKinds kinds;
        op->body.accept(&kinds);
        // An allocation with a loop over lanes inside it is warp-level
        // storage, which LowerWarpShuffles stripes across the lanes. Leave it
        // alone. Without a loop over threads there is nowhere to put this one,
        // and whoever runs it already has it to themselves.
        if (!in_threads && op->memory_type == MemoryType::Register &&
            kinds.threads && !kinds.lanes) {
            // Pick it up, and put it back inside the loops over threads.
            pending.push_back(op);
            return mutate(op->body);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const For *op) override {
        if (op->for_type != ForType::GPUThread || pending.empty()) {
            ScopedValue<bool> bind(in_threads,
                                   in_threads || op->for_type == ForType::GPUThread ||
                                       op->for_type == ForType::GPULane);
            return IRMutator::visit(op);
        }

        // The outermost loop over threads with allocations to place. Everything
        // private to a thread goes inside it.
        vector<const Allocate *> allocs;
        allocs.swap(pending);

        Stmt body = op->body;
        for (const Allocate *alloc : allocs) {
            body = promote(alloc, body);
        }
        {
            ScopedValue<bool> bind(in_threads, true);
            body = mutate(body);
        }
        return op->with(op->min, op->max, body);
    }

    // Give each site its own registers, and wrap the body in the smaller
    // allocation.
    Stmt promote(const Allocate *op, Stmt body) {
        FindAccesses finder(op->name);
        body.accept(&finder);

        // Each access covers a set of elements, and get_subtile partitions the
        // accesses between the distinct sets. Nothing about the layout of a set
        // matters here, because the registers it gets are its own, so a dense
        // ramp reaches all of them.
        vector<MultiRamp> subtiles;
        map<Expr, Expr, IRDeepCompare> rewritten;
        string description = "the allocation " + op->name +
                             ", which is scheduled to live in Register memory outside the "
                             "loops over GPU threads";
        for (const Expr &index : finder.indices) {
            int subtile = get_subtile(index, description, &subtiles);
            // Every subtile has the same shape, and so the same number of
            // lanes, because get_subtile rejects accesses that don't.
            int lanes = subtiles[subtile].total_lanes();
            Expr base = make_const(index.type().element_of(), subtile * lanes);
            rewritten[index] =
                lanes == 1 ? base : Ramp::make(base, make_one(base.type()), lanes);
        }

        int size = subtiles.empty() ? 0 : (int)subtiles.size() * subtiles[0].total_lanes();
        body = RewriteAccesses(op->name, rewritten).mutate(body);

        return op->with({make_const(Int(32), size)}, op->condition, body);
    }
};

}  // namespace

Stmt promote_gpu_registers(const Stmt &s) {
    return PromoteGPURegisters().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
