#include <algorithm>
#include <cmath>
#include <utility>

#include "Bounds.h"
#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "ExprUsesVar.h"
#include "FuseGPUThreadLoops.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Monotonic.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::sort;
using std::string;
using std::vector;

namespace {

// Being filled by the copy engine is a property of the stores to an
// allocation, not of the memory it lives in, so MemoryType::GPUSharedAsync is
// only the schedule's way of saying it. Rewrite such allocations to ordinary
// shared memory, wrapping the value of each store to them in a
// cuda_bypass_registers intrinsic and awaiting the copies where the data is
// consumed. Everything below then sees one kind of shared memory, which is
// what lets all of a kernel's shared allocations be packed together.
class MarkAsyncCopies : public IRMutator {
    using IRMutator::visit;

    // The allocations being filled by the copy engine, and the group each
    // one's copies belong to. Copies in a group are awaited together.
    map<string, int> groups;
    int next_group = 0;

    // How many iterations ahead each Func's producer runs, from the depth on
    // its slide directive. A pipelined producer's wait lets that many batches
    // of its copies keep flying.
    const map<string, int> &slide_depths;

    int depth_of(const string &name) const {
        auto it = slide_depths.find(name);
        return it == slide_depths.end() ? 0 : it->second;
    }

    // Only CUDA has a copy engine to drive. Elsewhere the allocation still
    // becomes ordinary shared memory, but the stores to it stay ordinary too.
    DeviceAPI device_api = DeviceAPI::None;

    Stmt visit(const For *op) override {
        ScopedValue<DeviceAPI> d(device_api, op->device_api == DeviceAPI::None ?
                                                 device_api :
                                                 op->device_api);
        return IRMutator::visit(op);
    }

    Stmt visit(const Allocate *op) override {
        if (op->memory_type != MemoryType::GPUSharedAsync) {
            return IRMutator::visit(op);
        }

        // One group per allocation, so that consuming one Func doesn't wait
        // for the copies into another.
        auto [it, inserted] = groups.emplace(op->name, next_group++);
        internal_assert(inserted)
            << "Two asynchronously copied allocations are both named " << op->name << "\n";

        Stmt body = mutate(op->body);
        groups.erase(it);

        return Allocate::make(op->name, op->type, MemoryType::GPUShared,
                              op->extents, mutate(op->condition), body,
                              op->new_expr, op->free_function, op->padding);
    }

    Stmt visit(const Store *op) override {
        auto it = groups.find(op->name);
        if (it == groups.end() || device_api != DeviceAPI::CUDA) {
            return IRMutator::visit(op);
        }
        // The Func name is carried along because the allocations get packed
        // together below, after which the store no longer knows which Func it
        // belongs to, and that is the name an error has to name.
        Expr value = Call::make(op->value.type(), Call::cuda_bypass_registers,
                                {mutate(op->value), it->second, StringImm::make(op->name)},
                                Call::Intrinsic);
        return Store::make(op->name, value, mutate(op->index), op->param,
                           mutate(op->predicate), op->alignment, op->is_streaming);
    }

    // Waiting for a copy is something each thread does for the copies it
    // issued itself, so the wait belongs inside the loops over threads. Put it
    // at the end of the innermost one, which is still before the barrier that
    // publishes the data to the rest of the block and before any of it is
    // read. Leaving it after them would make it a statement outside the loops
    // over threads, which is a thing only the first thread does.
    class AwaitInEachThread : public IRMutator {
        using IRMutator::visit;

        Stmt visit(const For *op) override {
            Stmt body = mutate(op->body);
            if ((op->for_type == ForType::GPUThread || op->for_type == ForType::GPULane) &&
                body.same_as(op->body)) {
                // Placing the wait is the only thing this does, so a body that
                // comes back unchanged is one with no loop over threads inside
                // it, and this is the innermost one.
                body = Block::make(body, Evaluate::make(wait));
            }
            return op->with(op->min, op->max, body);
        }

        const Expr &wait;

    public:
        using IRMutator::mutate;

        AwaitInEachThread(const Expr &wait)
            : wait(wait) {
        }
    };

    static bool issues_copies(const Stmt &s) {
        class Finder : public IRVisitor {
            using IRVisitor::visit;
            void visit(const Call *op) override {
                found |= op->is_intrinsic(Call::cuda_bypass_registers);
                IRVisitor::visit(op);
            }

        public:
            bool found = false;
        } finder;
        s.accept(&finder);
        return finder.found;
    }

    // A software-pipelined producer's body has more than one sliver's copies
    // in it: the sliver it is running ahead to fill, and on the first
    // iteration the slivers that warm the window up. The wait can only tell
    // them apart if they are separate batches, so close a batch after each
    // top-level piece of the body that issues copies, except the last, whose
    // batch the wait itself closes. Each piece holds its own loops over
    // threads, and a commit is each thread closing the batch of copies it
    // issued itself, so it goes at the end of those loops, exactly where the
    // wait goes in the piece that keeps it. An empty batch retires
    // immediately, so a piece whose copies are guarded off this iteration
    // closes one for free.
    static Stmt commit_between_segments(const Stmt &s) {
        Expr commit = Call::make(Int(32), Call::cuda_commit_copies, {}, Call::Intrinsic);
        if (const LetStmt *let = s.as<LetStmt>()) {
            Stmt body = commit_between_segments(let->body);
            return body.same_as(let->body) ? s : LetStmt::make(let->name, let->value, body);
        }
        const Block *b = s.as<Block>();
        if (!b) {
            return s;
        }
        Stmt rest = commit_between_segments(b->rest);
        Stmt first = b->first;
        if (issues_copies(first) && issues_copies(rest)) {
            // Not the last copy-issuing piece, so its batch closes here.
            Stmt with_commit = AwaitInEachThread(commit).mutate(first);
            first = with_commit.same_as(first) ?
                        Block::make(first, Evaluate::make(commit)) :
                        with_commit;
        }
        if (first.same_as(b->first) && rest.same_as(b->rest)) {
            return s;
        }
        return Block::make(first, rest);
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body = mutate(op->body);
        auto it = groups.find(op->name);
        if (op->is_producer && it != groups.end() && device_api == DeviceAPI::CUDA) {
            int depth = depth_of(op->name);
            if (depth > 0) {
                body = commit_between_segments(body);
            }
            Expr wait = Call::make(Int(32), Call::cuda_await_copies,
                                   {it->second, depth}, Call::Intrinsic);
            Stmt with_wait = AwaitInEachThread(wait).mutate(body);
            // Unchanged means there was no loop over threads to put it in,
            // because only one thread runs this producer.
            body = with_wait.same_as(body) ? Block::make(body, Evaluate::make(wait)) : with_wait;
        }
        return op->with(body);
    }

public:
    using IRMutator::mutate;

    MarkAsyncCopies(const map<string, int> &slide_depths)
        : slide_depths(slide_depths) {
    }
};

// The copy engine moves up to 16 bytes at a time, and needs its destination
// aligned to the width of the copy. Allocations are packed one after another,
// so a group has to end on a 16-byte boundary for whatever follows it to be
// copyable into. Group sizes are counted in units of the type they are
// allocated as, so dividing gives the number of those units to align to. Units
// of 16 bytes or more are big enough already and give zero, which align_up
// takes to mean no alignment at all.
const int async_copy_alignment = 16;

class ExtractBlockSize : public IRVisitor {
protected:
    Expr block_extent[3], block_count[3];
    string block_var_name[3];

    using IRVisitor::visit;

    void found_thread_for(int dim, const string &name, const Expr &extent) {
        internal_assert(dim >= 0 && dim < 3);
        if (!block_extent[dim].defined()) {
            block_extent[dim] = simplify(extent);
        } else {
            block_extent[dim] = simplify(Max::make(extent, block_extent[dim]));
        }
    }

    void found_block_for(int dim, const string &name, Expr extent) {
        internal_assert(dim >= 0 && dim < 3);
        internal_assert(!block_count[dim].defined());
        block_count[dim] = std::move(extent);
        block_var_name[dim] = name;
    }

    void visit(const For *op) override {
        for (int i = 0; i < 3; i++) {
            if (ends_with(op->name, gpu_thread_name(i))) {
                found_thread_for(i, op->name, op->extent());
            } else if (ends_with(op->name, gpu_block_name(i))) {
                found_block_for(i, op->name, op->extent());
            }
        }

        IRVisitor::visit(op);

        Scope<Interval> scope;
        scope.push(op->name, Interval(op->min, op->max));
        // For non-rectangular thread loops, use a bounding box. We'll inject if statements later.
        for (Expr &e : block_extent) {
            if (e.defined() && expr_uses_var(e, op->name)) {
                e = simplify(common_subexpression_elimination(e));
                e = simplify(bounds_of_expr_in_scope(e, scope).max);
            }
        }
    }

    void visit(const LetStmt *op) override {
        IRVisitor::visit(op);
        for (Expr &e : block_extent) {
            if (e.defined() &&
                expr_uses_var(e, op->name)) {
                e = simplify(Let::make(op->name, op->value, e));
            }
        }
    }

public:
    int blocks_dimensions() const {
        for (int i = 0; i < 3; i++) {
            if (!block_count[i].defined()) {
                return i;
            }
        }
        return 3;
    }

    int threads_dimensions() const {
        for (int i = 0; i < 3; i++) {
            if (!block_extent[i].defined()) {
                return i;
            }
        }
        return 3;
    }

    Expr num_threads(int d) const {
        return block_extent[d];
    }

    Expr num_blocks(int d) const {
        return block_count[d];
    }

    Expr block_var(int d) const {
        // The name of the actual for loop
        return Variable::make(Int(32), block_var_name[d]);
    }

    Expr thread_var(int d) const {
        // Thread variables get canonical names
        return Variable::make(Int(32), gpu_thread_name(d));
    }
};

class NormalizeDimensionality : public IRMutator {
protected:
    using IRMutator::visit;

    const ExtractBlockSize &block_size;
    const DeviceAPI device_api;

    int depth = 0;
    // Bitmask of the thread dimensions the subtree being wrapped loops over.
    int dims_used = 0;

    Stmt wrap(Stmt s) {
        if (depth != 0) {
            return mutate(s);
        }
        int outer_dims_used = dims_used;
        dims_used = 0;
        s = mutate(s);
        if (is_no_op(s)) {
            dims_used = outer_dims_used;
            return s;
        }
        // Give it a degenerate loop over each dimension it lacks. Which
        // dimensions those are can't be inferred from the depth of the nest: a
        // loop over lanes is a loop over x, so a nest that has one but not the
        // other is possible in either order.
        const int all_dims = (1 << block_size.threads_dimensions()) - 1;
        for (int d = 0; d < block_size.threads_dimensions(); d++) {
            if (!(dims_used & (1 << d))) {
                s = For::make(gpu_thread_name(d), 0, 0, ForType::GPUThread,
                              Partition::Never, device_api, s);
            }
        }
        // Anything enclosing this now loops over every dimension too.
        dims_used = outer_dims_used | all_dims;
        return s;
    }

    Stmt visit(const Block *op) override {
        Stmt first = wrap(op->first);

        Stmt rest;
        if (op->rest.defined()) {
            rest = wrap(op->rest);
        }

        if (first.same_as(op->first) &&
            rest.same_as(op->rest)) {
            return op;
        } else {
            return Block::make(first, rest);
        }
    }

    Stmt visit(const For *op) override {
        if (op->for_type == ForType::GPUThread ||
            op->for_type == ForType::GPULane) {
            depth++;
            for (int d = 0; d < 3; d++) {
                if (ends_with(op->name, gpu_thread_name(d))) {
                    dims_used |= 1 << d;
                }
            }
            Stmt stmt = IRMutator::visit(op);
            depth--;
            return stmt;
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    NormalizeDimensionality(const ExtractBlockSize &e, DeviceAPI device_api)
        : block_size(e), device_api(device_api) {
    }
};

class ReplaceForWithIf : public IRMutator {
protected:
    using IRMutator::visit;

    const ExtractBlockSize &block_size;

    Stmt visit(const For *op) override {
        if (op->for_type == ForType::GPUThread ||
            op->for_type == ForType::GPULane) {
            int dim;
            for (dim = 0; dim < 3; dim++) {
                if (ends_with(op->name, gpu_thread_name(dim))) {
                    break;
                }
            }

            internal_assert(dim >= 0 && dim < block_size.threads_dimensions());

            Stmt body = mutate(op->body);

            Expr var = Variable::make(Int(32), gpu_thread_name(dim));
            body = substitute(op->name, var + op->min, body);

            if (can_prove(op->extent() == block_size.num_threads(dim))) {
                return body;
            } else {
                Expr cond = var <= op->max;
                return IfThenElse::make(cond, body, Stmt());
            }
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    ReplaceForWithIf(const ExtractBlockSize &e)
        : block_size(e) {
    }
};

// An allocation inside the thread loops stays in register/local memory
// (handled by ExtractRegisterAllocations) rather than being pulled out to the
// block level (handled by ExtractSharedAndHeapAllocations) if it has a fixed
// size or an explicit register/stack/fragment memory type. A tensor core
// fragment is per-lane, so it belongs in registers wherever it was declared.
bool allocation_goes_to_registers(const Allocate *op, bool in_threads) {
    bool fixed_size_thread_allocation = (op->constant_allocation_size() != 0) && in_threads;
    return (fixed_size_thread_allocation &&
            op->memory_type != MemoryType::Heap &&
            !is_gpu_shared(op->memory_type) &&
            op->memory_type != MemoryType::GPUTexture) ||
           op->memory_type == MemoryType::Register ||
           op->memory_type == MemoryType::Stack ||
           op->memory_type == MemoryType::Tile;
}

// Rename an allocation and all of its loads, stores, and frees. Relies on the
// invariant that no allocation of the same name is nested inside this one, so
// every reference in the body belongs to it.
class ExtractSharedAndHeapAllocations : public IRMutator {
protected:
    using IRMutator::visit;

    struct IntInterval {
        IntInterval()
            : IntInterval(0, 0) {
        }
        IntInterval(int min, int max)
            : min(min), max(max) {
        }
        int min;
        int max;
    };

    struct SharedAllocation {
        string name;
        Type type;
        Expr size;
        IntInterval liveness;    // Start and end of the barrier stage at which this allocation is used.
        MemoryType memory_type;  // Should be GPUShared or Heap
        bool striped_over_threads;
        bool size_computed_on_host;
    };

    struct AllocGroup {
        AllocGroup() = default;
        AllocGroup(const SharedAllocation &alloc)
            : widest_type(alloc.type),
              max_size(alloc.size),
              memory_type(alloc.memory_type) {
            group.push_back(alloc);
        }

        void insert(const SharedAllocation &alloc) {
            internal_assert(alloc.memory_type == memory_type);
            if (alloc.type.bytes() == widest_type.bytes()) {
                max_size = max(max_size, alloc.size);
            } else if (alloc.type.bytes() > widest_type.bytes()) {
                // Change units of max_size
                int size_ratio = alloc.type.bytes() / widest_type.bytes();
                max_size = max(max_size / size_ratio, alloc.size);
                widest_type = alloc.type;
            } else {
                int size_ratio = widest_type.bytes() / alloc.type.bytes();
                max_size = max(max_size, alloc.size / size_ratio);
            }
            group.push_back(alloc);
        }

        // Only need to check the back of the vector since we always insert
        // the most recent allocation at the back.
        bool is_free(int stage) const {
            return group.back().liveness.max < stage;
        }

        Type widest_type;
        Expr max_size;                   // In units of the widest type
        vector<SharedAllocation> group;  // Groups of allocs that should be coalesced together
        MemoryType memory_type;          // All allocations in the group have this memory type
    };

public:
    vector<SharedAllocation> allocations;

    /** Which allocations end up sharing one piece of memory. Two Funcs in the
     * same group are the same bytes at different times, so a hazard between
     * them is a hazard on that memory even though the names differ, and
     * whoever places thread barriers has to see them as one thing. The groups
     * are decided by liveness measured here, before any barriers are placed,
     * so asking now gives the same answer as rewrapping later will. */
    std::map<std::string, int> storage_groups() {
        vector<SharedAllocation> copy = allocations;
        vector<AllocGroup> groups = allocate_funcs(copy);
        std::map<std::string, int> result;
        for (int i = 0; i < (int)groups.size(); i++) {
            for (const SharedAllocation &a : groups[i].group) {
                result[a.name] = i;
            }
        }
        return result;
    }

protected:
    map<string, SharedAllocation *> shared;

    bool in_threads = false;

    int barrier_stage = 0;

    const DeviceAPI device_api;

    string thread_id_var_name, num_threads_var_name;

    const bool may_merge_allocs_of_different_type =
        device_api != DeviceAPI::D3D12Compute &&
        device_api != DeviceAPI::Vulkan &&
        device_api != DeviceAPI::WebGPU;

    // A loop on the host used to compute the shared memory size
    Stmt host_side_preamble;

    void precompute_allocation_size(SharedAllocation &s) {
        Expr val = Load::make(Int(32), s.name + ".shared_size", 0,
                              Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{}, false);
        Stmt update_size = Store::make(s.name + ".shared_size", max(s.size, val), 0,
                                       Parameter{}, const_true(), ModulusRemainder{}, false);

        if (host_side_preamble.defined()) {
            host_side_preamble = Block::make(host_side_preamble, update_size);
        } else {
            host_side_preamble = update_size;
        }
        s.size_computed_on_host = true;
        s.size = Variable::make(Int(32), s.name + ".shared_size_var");
    }

    Stmt visit(const For *op) override {
        bool is_thread_loop = op->for_type == ForType::GPUThread || op->for_type == ForType::GPULane;
        ScopedValue<bool> old_in_threads(in_threads, in_threads || is_thread_loop);

        // Set aside the allocations we've found so far.
        vector<SharedAllocation> old;
        old.swap(allocations);

        // And any preamble
        Stmt old_preamble = host_side_preamble;
        host_side_preamble = Stmt();

        // Find allocations inside the loop body
        Stmt body = mutate(op->body);

        // Expand any new shared allocations found in the body using the loop bounds.
        Scope<Interval> scope;
        scope.push(op->name, Interval(op->min, op->max));
        for (SharedAllocation &s : allocations) {
            // If the size depends on the loop variable, take the max
            // over all loop iterations
            if (expr_uses_var(s.size, op->name) && !s.size_computed_on_host) {
                s.size = simplify(common_subexpression_elimination(s.size));
                // It's worth working extra hard to remove any
                // repeated dependence on the block var
                s.size = solve_expression(s.size, op->name).result;
                s.size = simplify(common_subexpression_elimination(s.size));
                switch (is_monotonic(s.size, op->name)) {
                case Monotonic::Unknown:
                    // TODO: if bounds_of_expr_in_scope becomes more
                    // powerful than is_monotonic, it might be better
                    // to call it here. That would be risky though, as
                    // it's not exact.
                    debug(1)
                        << "Shared allocation for " << s.name
                        << " has a size that is non-monotonic in the gpu block variable " << op->name
                        << ": " << s.size << "\n";
                    precompute_allocation_size(s);
                    break;
                case Monotonic::Increasing:
                    s.size = substitute(op->name, op->max, s.size);
                    break;
                case Monotonic::Constant:
                    // The size expression used the variable, but we
                    // may have successfully eliminated it above, or
                    // is_monotonic might have detected that the
                    // dependence is false somehow. Just treat it as
                    // decreasing...
                case Monotonic::Decreasing:
                    s.size = substitute(op->name, op->min, s.size);
                    break;
                }
            }
            if (in_threads && op->is_parallel()) {
                // For parallel inner loops, make a separate slice per loop iteration
                s.size *= op->extent();
            }
        }

        // Add back on the allocations we set aside.
        if (!allocations.empty()) {
            allocations.insert(allocations.end(), old.begin(), old.end());
        } else {
            allocations.swap(old);
        }

        Expr new_min = mutate(op->min);
        Expr new_max = mutate(op->max);

        if (host_side_preamble.defined()) {
            string loop_name = unique_name('t');
            Expr v = Variable::make(Int(32), loop_name);
            host_side_preamble = substitute(op->name, v, host_side_preamble);
            host_side_preamble = For::make(loop_name, new_min, new_max,
                                           ForType::Serial, Partition::Never, DeviceAPI::None, host_side_preamble);
            if (old_preamble.defined()) {
                host_side_preamble = Block::make(old_preamble, host_side_preamble);
            }
        } else {
            host_side_preamble = old_preamble;
        }

        return op->with(new_min, new_max, body);
    }

    Stmt visit(const Block *op) override {
        if (!in_threads && op->rest.defined()) {
            Stmt first = mutate(op->first);
            barrier_stage++;
            Stmt rest = mutate(op->rest);

            if (first.same_as(op->first) &&
                rest.same_as(op->rest)) {
                return op;
            } else {
                return Block::make(first, rest);
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        Expr condition = mutate(op->condition);
        Stmt before_preamble = host_side_preamble;
        host_side_preamble = Stmt();
        Stmt then_case = mutate(op->then_case);
        Stmt then_preamble = host_side_preamble;
        host_side_preamble = Stmt();
        Stmt else_case = mutate(op->else_case);
        Stmt else_preamble = host_side_preamble;

        if (then_preamble.defined()) {
            host_side_preamble = IfThenElse::make(condition, then_preamble, else_preamble);
        } else if (else_preamble.defined()) {
            host_side_preamble = IfThenElse::make(!condition, else_preamble);
        }
        if (before_preamble.defined() && host_side_preamble.defined()) {
            host_side_preamble = Block::make(before_preamble, host_side_preamble);
        } else if (before_preamble.defined()) {
            host_side_preamble = before_preamble;
        }
        return IfThenElse::make(condition, then_case, else_case);
    }

    Stmt visit(const Allocate *op) override {
        user_assert(!op->new_expr.defined())
            << "Allocate node inside GPU kernel has custom new expression.\n"
            << "(Memoization is not supported inside GPU kernels at present.)\n";

        if (allocation_goes_to_registers(op, in_threads)) {
            // These allocations go in register or local memory
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Auto ||
                    is_gpu_shared(op->memory_type) ||
                    op->memory_type == MemoryType::GPUTexture ||
                    op->memory_type == MemoryType::Heap)
            << "Allocation " << op->name << " must live in shared or heap memory, "
            << "but is scheduled to live in " << op->memory_type << " memory.\n";

        SharedAllocation alloc;
        alloc.name = op->name;
        alloc.type = op->type;
        alloc.liveness = IntInterval(barrier_stage, barrier_stage);
        alloc.size = 1;
        for (const auto &extent : op->extents) {
            alloc.size *= extent;
        }
        alloc.size = simplify(alloc.size);
        alloc.memory_type = op->memory_type;
        alloc.size_computed_on_host = false;
        alloc.striped_over_threads = in_threads;

        if (alloc.memory_type == MemoryType::Auto) {
            if (in_threads) {
                // Dynamic allocation within the threads loop go on
                // the heap by default.
                alloc.memory_type = MemoryType::Heap;
            } else {
                // Allocations at the blocks level go in shared by
                // default.
                alloc.memory_type = MemoryType::GPUShared;
            }
        }

        // Updates the liveness by checking for all uses
        shared.emplace(op->name, &alloc);
        Stmt stmt = IRMutator::visit(op);
        op = stmt.as<Allocate>();
        internal_assert(op);

        allocations.push_back(alloc);
        shared.erase(op->name);
        return op->body;
    }

    Expr mutate_index(SharedAllocation *alloc, const Expr &index) {
        Expr idx = mutate(index);
        if (alloc->striped_over_threads) {
            idx *= Variable::make(Int(32), num_threads_var_name);
            idx += Variable::make(Int(32), thread_id_var_name);
        }
        return idx;
    }

    Expr visit(const Load *op) override {
        auto it = shared.find(op->name);
        if (it != shared.end()) {
            SharedAllocation *alloc = it->second;
            alloc->liveness.max = barrier_stage;
            // The allocation keeps its name, so we only need to rewrite the
            // node when the storage is striped across threads.
            if (alloc->striped_over_threads) {
                return Load::make(op->type, op->name, mutate_index(alloc, op->index),
                                  op->image, op->param, mutate(op->predicate), op->alignment, op->is_streaming);
            }
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        auto it = shared.find(op->name);
        if (it != shared.end()) {
            SharedAllocation *alloc = it->second;
            alloc->liveness.max = barrier_stage;
            if (alloc->striped_over_threads) {
                return Store::make(op->name, mutate(op->value), mutate_index(alloc, op->index),
                                   op->param, mutate(op->predicate), op->alignment, op->is_streaming);
            }
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);

        // Set aside the allocations we've found so far.
        Stmt old_preamble = host_side_preamble;
        host_side_preamble = Stmt();
        vector<SharedAllocation> old;
        old.swap(allocations);

        Stmt body = mutate(op->body);

        // Wrap let expression for any allocations found within
        for (SharedAllocation &s : allocations) {
            if (expr_uses_var(s.size, op->name) && !s.size_computed_on_host) {
                s.size = Let::make(op->name, op->value, s.size);
                s.size = simplify(s.size);
            }
        }

        if (host_side_preamble.defined() &&
            stmt_uses_var(host_side_preamble, op->name)) {
            host_side_preamble = op->with(op->value, host_side_preamble);
        }

        if (old_preamble.defined()) {
            if (host_side_preamble.defined()) {
                host_side_preamble = Block::make(old_preamble, host_side_preamble);
            } else {
                host_side_preamble = old_preamble;
            }
        }

        // Add back on the allocations we set aside.
        if (!allocations.empty()) {
            allocations.insert(allocations.end(), old.begin(), old.end());
        } else {
            allocations.swap(old);
        }

        if (op->body.same_as(body) && value.same_as(op->value)) {
            return op;
        } else {
            return op->with(value, body);
        }
    }

    // Return index to free_spaces where 'alloc' should be coalesced. Return -1
    // if there isn't any.
    int find_best_fit(const vector<AllocGroup> &mem_allocs,
                      const vector<int> &free_spaces,
                      const SharedAllocation &alloc, int stage) {
        int free_idx = -1;

        Expr alloc_size = simplify(alloc.size);

        // We prefer to coalesce dynamic-sized allocation with a dynamic-sized one and
        // constant-sized alloc with a constant-sized one. If we can't find any free
        // space with a matching type, we pick the most-recently freed space of the
        // other type (e.g. pick constant-sized free space for a dynamic-sized allocation
        // and vice versa). We prefer the most-recently freed space as stages that are
        // close together usually have relatively similar allocation size. For
        // constant-sized allocation, we prioritize free space which size differs
        // the least with 'alloc' (can be smaller or larger; it does not really
        // matter since we take the max of the two as the new size).

        if (!is_const(alloc_size)) {  // dynamic-sized alloc
            for (int i = free_spaces.size() - 1; i >= 0; --i) {
                internal_assert(free_spaces[i] >= 0 && free_spaces[i] < (int)mem_allocs.size());
                internal_assert(mem_allocs[free_spaces[i]].is_free(stage));

                if (mem_allocs[free_spaces[i]].memory_type != alloc.memory_type) {
                    continue;
                }

                if (!may_merge_allocs_of_different_type &&
                    mem_allocs[free_spaces[i]].group[0].type != alloc.type) {
                    continue;
                }

                if (!is_const(mem_allocs[free_spaces[i]].max_size)) {
                    return i;
                } else if (free_idx == -1) {
                    free_idx = i;
                }
            }
        } else {  // constant-sized alloc
            int64_t diff = -1;
            for (int i = free_spaces.size() - 1; i >= 0; --i) {
                internal_assert(free_spaces[i] >= 0 && free_spaces[i] < (int)mem_allocs.size());
                internal_assert(mem_allocs[free_spaces[i]].is_free(stage));

                if (mem_allocs[free_spaces[i]].memory_type != alloc.memory_type) {
                    continue;
                }

                if (!may_merge_allocs_of_different_type &&
                    mem_allocs[free_spaces[i]].group[0].type != alloc.type) {
                    continue;
                }

                if (is_const(mem_allocs[free_spaces[i]].max_size)) {
                    const auto &candidate_group = mem_allocs[free_spaces[i]];
                    Expr size = alloc_size * alloc.type.bytes();
                    Expr dist = candidate_group.max_size * candidate_group.widest_type.bytes() - size;
                    auto current_diff = as_const_int(simplify(dist));
                    internal_assert(current_diff);
                    int64_t abs_diff = std::abs(*current_diff);
                    if ((free_idx == -1) || (abs_diff < diff)) {
                        diff = abs_diff;
                        free_idx = i;
                    }
                } else if (free_idx == -1) {
                    free_idx = i;
                }
            }
        }

        return free_idx;
    }

    // Given some allocations, return a vector of allocation group where each group
    // consists of a number of allocations which should be coalesced together
    // in the shared memory.
    vector<AllocGroup> allocate_funcs(vector<SharedAllocation> &allocations) {
        // Sort based on the ascending order of the min liveness stage,
        // then sort based on the ascending order of the max liveness stage.
        sort(allocations.begin(), allocations.end(),
             [](const SharedAllocation &lhs, const SharedAllocation &rhs) {
                 if (lhs.liveness.min < rhs.liveness.min) {
                     return true;
                 } else if (lhs.liveness.min == rhs.liveness.min) {
                     return lhs.liveness.max < rhs.liveness.max;
                 }
                 return false;
             });

        vector<AllocGroup> mem_allocs;
        vector<int> free_spaces;  // Contains index to free spaces in mem_allocs
        int start_idx = 0;

        for (int stage = 0; stage <= barrier_stage; ++stage) {
            for (int i = start_idx; i < (int)allocations.size(); ++i) {
                if (allocations[i].liveness.min > stage) {
                    break;
                } else if (allocations[i].liveness.min == stage) {  // Allocate
                    int free_idx = find_best_fit(mem_allocs, free_spaces, allocations[i], stage);
                    if (free_idx != -1) {
                        mem_allocs[free_spaces[free_idx]].insert(allocations[i]);
                        free_spaces.erase(free_spaces.begin() + free_idx);
                    } else {
                        mem_allocs.emplace_back(allocations[i]);
                    }
                } else if (allocations[i].liveness.max == stage - 1) {  // Free
                    int free_idx = -1;
                    for (int j = 0; j < (int)mem_allocs.size(); ++j) {  // Find the index of the space to free
                        if (mem_allocs[j].group.back().name == allocations[i].name) {
                            free_idx = j;
                            break;
                        }
                    }
                    internal_assert(free_idx >= 0 && free_idx < (int)mem_allocs.size());
                    free_spaces.push_back(free_idx);
                    start_idx = i + 1;
                }
            }
        }

        return mem_allocs;
    }

    Expr get_block_id(const ExtractBlockSize &bs) const {
        Expr block_id = 0;
        for (int d = bs.blocks_dimensions() - 1; d >= 0; d--) {
            block_id *= bs.num_blocks(d);
            block_id += bs.block_var(d);
        }
        return block_id;
    }

    Expr max_over_blocks(const Expr &e, const ExtractBlockSize &bs) const {
        Scope<Interval> scope;
        for (int d = 0; d < bs.blocks_dimensions(); d++) {
            scope.push(bs.block_var(d).as<Variable>()->name,
                       Interval(0, bs.num_blocks(d) - 1));
        }
        Interval in = bounds_of_expr_in_scope(simplify(e), scope);
        if (in.has_upper_bound()) {
            return in.max;
        } else {
            return Expr();
        }
    }

    struct GlobalAllocation {
        string name;
        Expr size;
        Type type;
    };
    vector<GlobalAllocation> global_allocations;

public:
    // A single Func can be realized at several sites at the block level with
    // disjoint lifetimes (e.g. when a serial loop above the GPU threads is
    // unrolled), producing multiple allocations that share a name. Collapse
    // each such set into one allocation spanning the union of their lifetimes
    // and sized to the largest, so the Func keeps its own name instead of one
    // suffixed per copy. The loads and stores already all use that name, and
    // the host-side size computation already accumulates a max across the
    // copies, so nothing else needs rewriting.
    void merge_repeated_allocations() {
        vector<SharedAllocation> merged;
        map<string, size_t> index;
        for (SharedAllocation &s : allocations) {
            auto it = index.find(s.name);
            if (it == index.end()) {
                index[s.name] = merged.size();
                merged.push_back(s);
                continue;
            }
            SharedAllocation &m = merged[it->second];
            internal_assert(m.type == s.type &&
                            m.memory_type == s.memory_type &&
                            m.striped_over_threads == s.striped_over_threads &&
                            m.size_computed_on_host == s.size_computed_on_host)
                << "Mismatched allocations share the name " << s.name << "\n";
            internal_assert(m.liveness.max < s.liveness.min ||
                            s.liveness.max < m.liveness.min)
                << "Allocations sharing the name " << s.name
                << " have overlapping lifetimes and cannot be coalesced\n";
            m.liveness.min = std::min(m.liveness.min, s.liveness.min);
            m.liveness.max = std::max(m.liveness.max, s.liveness.max);
            if (!m.size_computed_on_host) {
                m.size = simplify(max(m.size, s.size));
            }
        }
        allocations.swap(merged);
    }

    // Run the mutator, then coalesce repeated allocations so callers always
    // see a finalized allocation list.
    Stmt operator()(const Stmt &s) {
        Stmt result = mutate(s);
        merge_repeated_allocations();
        return result;
    }

    Stmt rewrap_block(Stmt s, const ExtractBlockSize &bs) {

        // Combine the allocations into groups that have disjoint
        // lifetimes, and then cluster the groups according to which
        // ones can share a single allocation. For cuda, opencl, and
        // similar we get one big combined allocation per memory
        // type. For vulkan and direct3d, we also separate by
        // element type.
        map<pair<MemoryType, Type>, vector<AllocGroup>> clustered_allocs;

        {
            vector<AllocGroup> mem_allocs = allocate_funcs(allocations);

            // Every allocation must belong to one group
            internal_assert(allocations.size() >= mem_allocs.size());

            // Sort the allocations by the max size in bytes of the primitive
            // types in the group. Because the type sizes are then decreasing powers of
            // two, doing this guarantees that all allocations are aligned
            // to then element type as long as the original one is aligned
            // to the widest type.
            sort(mem_allocs.begin(), mem_allocs.end(),
                 [](const AllocGroup &lhs, const AllocGroup &rhs) {
                     return lhs.widest_type.bytes() > rhs.widest_type.bytes();
                 });

            for (const auto &alloc : mem_allocs) {
                Type t = may_merge_allocs_of_different_type ? UInt(8) : alloc.widest_type;
                pair<MemoryType, Type> key{alloc.memory_type, t};
                clustered_allocs[key].push_back(alloc);
            }
        }

        for (auto &p : clustered_allocs) {
            vector<AllocGroup> &cluster = p.second;
            // Heap or shared?
            MemoryType memory_type = p.first.first;
            // Type of the combined Allocate node
            Type alloc_type = p.first.second;

            // Figure out a name for the cluster, the total size of
            // the cluster (in terms of the alloc_type), and the
            // widest type in the cluster (which may be wider than the
            // alloc_type).
            int number_of_allocs = 0;
            for (const auto &alloc : cluster) {
                number_of_allocs += alloc.group.size();
            }

            // A single shared allocation needs no offset math, so we can name
            // the backing allocation after the Func directly and skip the
            // aliasing wrappers entirely, keeping the common case uncluttered.
            // Anything else (multiple fused allocations, or a heap allocation
            // that is sliced per-block out of a larger device allocation) gets
            // a fresh backing name plus one aliasing allocation per Func. The
            // per-Func names live on the aliasing allocations, so the backing
            // itself just needs a unique name.
            const bool simple = number_of_allocs == 1 && memory_type != MemoryType::Heap;

            string name;
            if (simple) {
                name = cluster[0].group[0].name;
            } else if (memory_type == MemoryType::Heap) {
                name = unique_name("global_alloc");
            } else {
                name = unique_name("shared_alloc");
            }

            Expr total_size = 0;
            Type widest_type = cluster[0].widest_type;
            for (const auto &alloc : cluster) {
                if (alloc.widest_type.bytes() > widest_type.bytes()) {
                    widest_type = alloc.widest_type;
                }
                int ratio = alloc.widest_type.bytes() / alloc_type.bytes();
                internal_assert(ratio != 0)
                    << "alloc_type should have been at most as wide as the widest type in group\n";
                // Sizes here are counted in units of one type or another, and
                // are converted between them by dividing, so the types have to
                // be whole multiples of each other.
                internal_assert(is_power_of_two(alloc_type.bytes()) &&
                                is_power_of_two(alloc.widest_type.bytes()))
                    << "Allocation types must be a power of two bytes wide, but these "
                    << "are " << alloc_type.bytes() << " and "
                    << alloc.widest_type.bytes() << "\n";
                total_size += align_up(alloc.max_size * ratio,
                                       async_copy_alignment / alloc_type.bytes());
            }

            // Upgrade the alloc type to the widest type found, and
            // downgrade total_size accordingly.
            int ratio = widest_type.bytes() / alloc_type.bytes();
            internal_assert(ratio != 0)
                << "alloc_type should have been at most as wide as the widest type in cluster\n";
            if (ratio != 1) {
                total_size += ratio - 1;
                total_size /= ratio;
            }
            alloc_type = widest_type;

            // Remove any dependence on the block vars by taking a max
            {
                Expr size = max_over_blocks(total_size, bs);
                internal_assert(size.defined())
                    << memory_type
                    << " memory used by GPU kernel varies with the block index in an unbounded way: "
                    << total_size << "\n";
                total_size = size;
            }

            const string total_size_name = name + ".size";
            Expr total_size_var = Variable::make(Int(32), total_size_name);

            // Wrap the body in one aliasing allocation per Func, each pointing
            // at its offset within the backing allocation. Loads and stores
            // keep their original per-Func names; the offsets get folded in
            // later by inject_gpu_offload, just before GPU codegen. The offsets
            // are in units of each allocation's own type; the group offsets
            // they build on are in units of widest_type across the cluster.
            if (!simple) {
                for (int i = (int)(cluster.size()) - 1; i >= 0; i--) {
                    Expr group_offset = Variable::make(Int(32), name + "." + std::to_string(i) + ".offset");
                    for (const SharedAllocation &alloc : cluster[i].group) {
                        Expr offset = group_offset;
                        internal_assert(alloc.type.bytes() <= widest_type.bytes());
                        if (alloc.type.bytes() < widest_type.bytes()) {
                            offset *= (widest_type.bytes() / alloc.type.bytes());
                        }
                        offset = simplify(offset);
                        Expr base = Variable::make(Handle(), name);
                        // Intrinsic, not PureIntrinsic: this keeps CSE/LICM from
                        // lifting it out of the aliasing Allocate's new_expr,
                        // which would move the reference to the backing
                        // allocation out of the backing allocation's own scope.
                        Expr aliased = Call::make(Handle(), Call::offset_pointer,
                                                  {base, offset}, Call::Intrinsic);
                        s = Allocate::make(alloc.name, alloc.type, alloc.memory_type,
                                           {alloc.size}, const_true(), s, aliased);
                    }
                }
            }

            // Make the backing allocation.
            if (memory_type == MemoryType::Heap) {
                global_allocations.push_back(GlobalAllocation{name, total_size, alloc_type});
            } else {
                s = Allocate::make(name, alloc_type, memory_type,
                                   {total_size_var}, const_true(), s);
            }

            // Define the group offsets, each in terms of the previous group in
            // the cluster.
            if (!simple) {
                for (int i = (int)(cluster.size()) - 1; i >= 0; i--) {
                    string group_offset_name = name + "." + std::to_string(i) + ".offset";
                    Expr offset;
                    if (i > 0) {
                        // Build off the last offset
                        offset = Variable::make(Int(32), name + "." + std::to_string(i - 1) + ".offset");
                        int ratio = (widest_type.bytes() / cluster[i - 1].widest_type.bytes());
                        internal_assert(ratio != 0);
                        offset += simplify(align_up((cluster[i - 1].max_size + ratio - 1) / ratio,
                                                    async_copy_alignment / widest_type.bytes()));
                    } else {
                        if (memory_type == MemoryType::Heap) {
                            // One slice of a larger global allocation
                            offset = get_block_id(bs) * total_size_var;
                        } else {
                            // Base address for shared memory is zero
                            offset = 0;
                        }
                    }
                    s = LetStmt::make(group_offset_name, simplify(offset), s);
                }
            }
            s = LetStmt::make(total_size_name, total_size, s);
        }

        // Resolve thread_id and threads_per_block variables, uses of
        // which were injected above if any allocation was striped
        // over the threads.
        Expr thread_id = 0, num_threads = 1;
        for (int d = bs.threads_dimensions() - 1; d >= 0; d--) {
            num_threads *= bs.num_threads(d);
            thread_id *= bs.num_threads(d);
            thread_id += bs.thread_var(d);
        }
        if (stmt_uses_var(s, thread_id_var_name)) {
            s = LetStmt::make(thread_id_var_name, thread_id, s);
        }
        if (stmt_uses_var(s, num_threads_var_name)) {
            s = LetStmt::make(num_threads_var_name, num_threads, s);
        }

        return s;
    }

    Stmt rewrap_kernel_launch(Stmt s, const ExtractBlockSize &bs, DeviceAPI device_api) {

        for (const auto &alloc : global_allocations) {
            Expr total_size = alloc.size;

            Expr device_interface = make_device_interface_call(device_api);
            string buffer_name = alloc.name + ".buffer";
            Expr buffer_var = Variable::make(type_of<halide_buffer_t *>(), buffer_name);

            BufferBuilder builder;
            builder.mins.emplace_back(0);
            builder.extents.push_back(total_size);
            builder.strides.emplace_back(1);
            builder.type = alloc.type;
            builder.dimensions = 1 + bs.blocks_dimensions();

            for (int d = 0; d < bs.blocks_dimensions(); d++) {
                Expr next_stride =
                    builder.strides.back() *
                    builder.extents.back();
                builder.strides.push_back(next_stride);
                builder.extents.emplace_back(bs.num_blocks(d));
            }
            Expr buffer = builder.build();
            Expr allocate_heap_call = Call::make(Int(32), "halide_device_malloc",
                                                 {buffer_var, device_interface}, Call::Extern);
            string allocate_heap_result_var_name = unique_name('t');
            Expr allocate_heap_result_var = Variable::make(Int(32), allocate_heap_result_var_name);
            Stmt check_allocated =
                AssertStmt::make(allocate_heap_result_var == 0, allocate_heap_result_var);
            Expr device_field = Call::make(Handle(), Call::buffer_get_device, {buffer_var}, Call::Extern);
            s = LetStmt::make(alloc.name, device_field, s);
            s = Block::make(check_allocated, s);
            s = LetStmt::make(allocate_heap_result_var_name, allocate_heap_call, s);
            s = Allocate::make(buffer_name, alloc.type,
                               MemoryType::Auto, {}, const_true(), s,
                               buffer, "halide_device_free_as_destructor");
        }

        s = compute_shared_memory_sizes_on_host(s);

        return s;
    }

    Stmt compute_shared_memory_sizes_on_host(Stmt result) {
        if (!host_side_preamble.defined()) {
            return result;
        }

        // Make all the let stmts that define the size vars
        for (auto &alloc : allocations) {
            if (alloc.size_computed_on_host) {
                string alloc_name = alloc.name + ".shared_size";
                string var_name = alloc.name + ".shared_size_var";
                Expr val = Load::make(Int(32), alloc_name, 0,
                                      Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{}, false);
                result = LetStmt::make(var_name, val, result);
                alloc.size = Variable::make(Int(32), var_name);
            }
        }

        // Prefix the preamble
        result = Block::make(host_side_preamble, result);

        // Wrap the preamble in all the allocation nodes
        for (auto &alloc : allocations) {
            if (alloc.size_computed_on_host) {
                string alloc_name = alloc.name + ".shared_size";
                Stmt init = Store::make(alloc_name, 0, 0,
                                        Parameter{}, const_true(), ModulusRemainder{}, false);
                result = Block::make(init, result);
                result = Allocate::make(alloc_name, Int(32), MemoryType::Stack, {1}, const_true(), result);
            }
        }

        return result;
    }

    // Returns the names of every allocation pulled out to the block level.
    std::set<string> allocation_names() const {
        std::set<string> names;
        for (const SharedAllocation &a : allocations) {
            names.insert(a.name);
        }
        return names;
    }

    ExtractSharedAndHeapAllocations(DeviceAPI d)
        : device_api(d),
          thread_id_var_name(unique_name('t')),
          num_threads_var_name(unique_name('t')) {
    }
};  // namespace Internal

// Pull out any allocate node outside of the innermost thread
// block. Should only be run after shared allocations have already
// been extracted.
class ExtractRegisterAllocations : public IRMutator {
protected:
    using IRMutator::visit;

    struct RegisterAllocation {
        string name;
        string loop_var;  // The nearest enclosing loop over threads. Empty if it's at block level.
        Type type;
        Expr size;
        MemoryType memory_type;  // Should be Auto, Stack, or Register
    };

    bool in_lane_loop = false;

    // Names of allocations pulled out to the block level, so we can rename any
    // register allocation that would otherwise collide with one.
    const std::set<string> block_alloc_names;

    Stmt visit(const For *op) override {
        ScopedValue<string> old_loop_var(loop_var);

        if (op->for_type == ForType::GPULane) {
            loop_var = op->name;
            internal_assert(!in_lane_loop);
            ScopedValue<bool> old_in_lane_loop(in_lane_loop, true);
            has_lane_loop = true;
            return IRMutator::visit(op);
        } else {
            if (op->for_type == ForType::GPUThread) {
                has_thread_loop = true;
                loop_var = op->name;
            }

            // Hoisting an allocation out of a vectorized for loop
            // would break here. We should already have hoisted
            // vectorized allocations.
            internal_assert(op->for_type != ForType::Vectorized);

            // Set aside the allocations we've found so far.
            vector<RegisterAllocation> old;
            old.swap(allocations);

            // Find allocations inside the loop body
            Stmt body = mutate(op->body);

            // Expand any new register allocations found in the body using the loop bounds.
            Scope<Interval> scope;
            scope.push(op->name, Interval(op->min, op->max));

            // Expand the inner allocations using the loop bounds.
            for (RegisterAllocation &s : allocations) {
                if (expr_uses_var(s.size, op->name)) {
                    s.size = bounds_of_expr_in_scope(s.size, scope).max;
                }
            }

            // Add back on the allocations we set aside.
            if (!allocations.empty()) {
                allocations.insert(allocations.end(), old.begin(), old.end());
            } else {
                allocations.swap(old);
            }

            return op->with(mutate(op->min), mutate(op->max), body);
        }
    }

    Stmt visit(const Allocate *op) override {
        if (in_lane_loop) {
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Stack ||
                    op->memory_type == MemoryType::Register ||
                    op->memory_type == MemoryType::Heap ||
                    op->memory_type == MemoryType::Auto ||
                    op->memory_type == MemoryType::Tile)
            << "Allocation " << op->name << " is scheduled inside a loop over GPU threads, so "
            << "it must live in stack memory, heap memory, or registers. "
            << "Shared allocations at this loop level are not yet supported.\n";

        // If this name also lives at the block level, rename this register copy
        // so it doesn't shadow the block allocation once it gets hoisted to wrap
        // the thread body. The block copy keeps the Func's name for the
        // profiler. By the no-shadowing invariant, every load/store of this name
        // in the body belongs to this allocation.
        string name = op->name;
        Stmt body = op->body;
        if (block_alloc_names.count(op->name)) {
            name = unique_name(op->name);
            const string &from = op->name;
            const string &to = name;
            body = mutate_with(
                body,
                [&](auto *self, const Load *load) -> Expr {
                    if (load->name == from) {
                        return Load::make(load->type, to, self->mutate(load->index), load->image, load->param,
                                          self->mutate(load->predicate), load->alignment, load->is_streaming);
                    }
                    return self->visit_base(load);
                },
                [&](auto *self, const Store *store) -> Stmt {
                    if (store->name == from) {
                        return Store::make(to, self->mutate(store->value), self->mutate(store->index), store->param,
                                           self->mutate(store->predicate), store->alignment, store->is_streaming);
                    }
                    return self->visit_base(store);
                },
                [&](auto *, const Free *free) -> Stmt {
                    if (free->name == from) {
                        return Free::make(to);
                    }
                    return free;
                });
        }

        RegisterAllocation alloc;
        alloc.name = name;
        alloc.type = op->type;
        alloc.size = 1;
        alloc.loop_var = loop_var;
        for (const auto &extent : op->extents) {
            alloc.size *= extent;
        }
        alloc.size = simplify(mutate(alloc.size));
        alloc.memory_type = op->memory_type;

        allocations.push_back(alloc);
        return mutate(body);
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        auto body = mutate(op->body);
        Expr value = mutate(op->value);

        for (RegisterAllocation &s : allocations) {
            if (expr_uses_var(s.size, op->name)) {
                s.size = simplify(Let::make(op->name, value, s.size));
            }
        }

        if (op->body.same_as(body) && op->value.same_as(value)) {
            return op;
        } else {
            return LetOrLetStmt::make(op->name, value, body);
        }
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    string loop_var;

public:
    vector<RegisterAllocation> allocations;

    ExtractRegisterAllocations(std::set<string> block_alloc_names)
        : block_alloc_names(std::move(block_alloc_names)) {
    }

    // Multiple realizations of the same Func inside the thread loops (e.g. from
    // unrolling a loop that holds a register allocation) share a name with
    // disjoint, sequential lifetimes. Coalesce them into one allocation sized
    // to the largest, so the Func keeps its own name (needed for profiler
    // attribution) and reuses the scarce register storage instead of getting
    // one array per copy. The loads and stores already all use that name.
    void merge_repeated_allocations() {
        vector<RegisterAllocation> merged;
        map<string, size_t> index;
        for (RegisterAllocation &s : allocations) {
            auto it = index.find(s.name);
            if (it == index.end()) {
                index[s.name] = merged.size();
                merged.push_back(s);
                continue;
            }
            RegisterAllocation &m = merged[it->second];
            internal_assert(m.type == s.type &&
                            m.memory_type == s.memory_type &&
                            m.loop_var == s.loop_var)
                << "Mismatched register allocations share the name " << s.name << "\n";
            m.size = simplify(max(m.size, s.size));
        }
        allocations.swap(merged);
    }

    // Run the mutator, then coalesce repeated allocations so callers always
    // see a finalized allocation list.
    Stmt operator()(const Stmt &s) {
        Stmt result = mutate(s);
        merge_repeated_allocations();
        return result;
    }

    Stmt rewrap(Stmt body, const string &loop_var) {
        for (RegisterAllocation &alloc : allocations) {
            if ((!loop_var.empty() && ends_with(alloc.loop_var, loop_var)) ||
                (loop_var.empty() && alloc.loop_var.empty())) {
                body = Allocate::make(alloc.name, alloc.type, alloc.memory_type, {alloc.size}, const_true(), body);
            }
        }
        return body;
    }

    bool has_lane_loop = false;
    bool has_thread_loop = false;
};

class InjectThreadBarriers : public IRMutator {
protected:
    bool in_threads = false, injected_barrier;

    using IRMutator::visit;

    const ExtractSharedAndHeapAllocations &block_allocs;
    const ExtractRegisterAllocations &register_allocs;

    // Names that share memory answer to the same key, so a hazard between two
    // Funcs the allocator coalesced is not missed.
    std::map<std::string, int> storage_group;

    std::string storage_key(const std::string &name) {
        auto it = storage_group.find(name);
        return it == storage_group.end() ? name : "group " + std::to_string(it->second);
    }


    MemoryType memory_type_for_name(const std::string &name) {
        for (const auto &x : register_allocs.allocations) {
            if (x.name == name) {
                return x.memory_type;
            }
        }
        for (const auto &x : block_allocs.allocations) {
            if (x.name == name) {
                return x.memory_type;
            }
        }
        // Not allocated here, so must assume it's input/output
        // of shader
        return MemoryType::Auto;
    }

    Stmt make_barrier(int mask) {
        return Evaluate::make(Call::make(Int(32), Call::gpu_thread_barrier,
                                         {IntImm::make(Int(32), mask)},
                                         Call::Intrinsic));
    }

    Stmt visit(const For *op) override {
        ScopedValue<bool> old_in_threads(in_threads,
                                         (in_threads ||
                                          op->for_type == ForType::GPUThread ||
                                          op->for_type == ForType::GPULane));

        ScopedValue<bool> old_injected_barrier(injected_barrier, false);

        if (!is_parallel(op->for_type)) {
            Stmt body = mutate(op->body);
            // Serial for loops at the block level with internal
            // synchronization also need synchronization after each
            // loop iteration.
            if (!in_threads && injected_barrier) {
                // Any memory access fences should be handled by the
                // synchronizations within the block
                body = Block::make(body, make_barrier(0));
            }
            return op->with(op->min, op->max, body);
        } else {
            return IRMutator::visit(op);
        }
    }

    // What a statement touches in the memory the threads of a block share.
    struct Footprint {
        std::set<std::string> shared_stores, shared_loads;
        std::set<std::string> device_stores, device_loads;

        static bool intersects(const std::set<std::string> &a,
                               const std::set<std::string> &b) {
            for (const auto &x : a) {
                if (b.count(x)) {
                    return true;
                }
            }
            return false;
        }

        // Which memory spaces this statement and everything before it since
        // the last barrier disagree about: it reads what was written, writes
        // what was read, or writes what was written. Any of those needs the
        // threads brought back together first.
        int conflict(const Footprint &earlier) const {
            int mask = 0;
            if (intersects(shared_loads, earlier.shared_stores) ||
                intersects(shared_stores, earlier.shared_loads) ||
                intersects(shared_stores, earlier.shared_stores)) {
                mask |= CodeGen_GPU_Dev::MemoryFenceType::Shared;
            }
            if (intersects(device_loads, earlier.device_stores) ||
                intersects(device_stores, earlier.device_loads) ||
                intersects(device_stores, earlier.device_stores)) {
                mask |= CodeGen_GPU_Dev::MemoryFenceType::Device;
            }
            return mask;
        }

        void add(const Footprint &other) {
            shared_stores.insert(other.shared_stores.begin(), other.shared_stores.end());
            shared_loads.insert(other.shared_loads.begin(), other.shared_loads.end());
            device_stores.insert(other.device_stores.begin(), other.device_stores.end());
            device_loads.insert(other.device_loads.begin(), other.device_loads.end());
        }
    };

    void record(Footprint &f, const std::string &raw_name, bool is_store) {
        const std::string name = storage_key(raw_name);
        switch (memory_type_for_name(raw_name)) {
        case MemoryType::GPUSharedAsync:
        case MemoryType::GPUShared:
            (is_store ? f.shared_stores : f.shared_loads).insert(name);
            break;
        case MemoryType::Auto:
        case MemoryType::Heap:
        case MemoryType::GPUTexture:
            (is_store ? f.device_stores : f.device_loads).insert(name);
            break;
        default:
            break;
        }
    }

    Footprint footprint_of(const Stmt &s) {
        Footprint f;
        visit_with(
            s,
            [&](auto *self, const Store *op) {
                record(f, op->name, true);
                self->visit_base(op);
            },
            [&](auto *self, const Load *op) {
                record(f, op->name, false);
                self->visit_base(op);
            });
        return f;
    }

    // A Block is a binary node, so a run of statements is a chain of them, and
    // Block::make reassociates so the chain leans right. Taking them a pair at
    // a time puts a barrier at every join, whether or not the two halves
    // disagree about any memory. Walk the whole chain and put barriers only
    // where a statement meets something an earlier one left.
    static void flatten(const Stmt &s, std::vector<Stmt> &into) {
        Stmt rest = s;
        while (const Block *b = rest.as<Block>()) {
            if (!b->rest.defined()) {
                break;
            }
            into.push_back(b->first);
            rest = b->rest;
        }
        into.push_back(rest);
    }

    Stmt visit(const Block *op) override {
        if (in_threads || !op->rest.defined()) {
            return IRMutator::visit(op);
        }

        std::vector<Stmt> stmts;
        flatten(op, stmts);

        std::vector<Stmt> result;
        Footprint pending;
        for (const Stmt &s : stmts) {
            Stmt mutated = mutate(s);
            Footprint here = footprint_of(mutated);
            int mask = here.conflict(pending);
            if (mask) {
                result.push_back(make_barrier(mask));
                injected_barrier = true;
                pending = Footprint();
            }
            pending.add(here);
            result.push_back(mutated);
        }
        return Block::make(result);
    }

public:
    InjectThreadBarriers(ExtractSharedAndHeapAllocations &sha, ExtractRegisterAllocations &ra)
        : block_allocs(sha),
          register_allocs(ra),
          storage_group(sha.storage_groups()) {
    }
};

class FuseGPUThreadLoopsSingleKernel : public IRMutator {
protected:
    using IRMutator::visit;
    const ExtractBlockSize &block_size;
    ExtractSharedAndHeapAllocations &block_allocations;

    Stmt visit(const For *op) override {
        if (ends_with(op->name, gpu_block_name(0))) {
            Stmt body = op->body;

            // This is the innermost loop over blocks.
            debug(3) << "Fusing thread block:\n"
                     << body << "\n\n";

            NormalizeDimensionality n(block_size, op->device_api);
            body = n(body);

            debug(3) << "Normalized dimensionality:\n"
                     << body << "\n\n";

            Expr block_size_x = block_size.threads_dimensions() ? block_size.num_threads(0) : 1;
            ExtractRegisterAllocations register_allocs(block_allocations.allocation_names());
            ForType innermost_loop_type = ForType::GPUThread;
            if (block_size.threads_dimensions()) {
                body = register_allocs(body);
                if (register_allocs.has_lane_loop) {
                    innermost_loop_type = ForType::GPULane;
                }
            }

            debug(3) << "Extracted register-level allocations:\n"
                     << body << "\n\n";

            if (register_allocs.has_thread_loop) {
                // If there's no loop over threads, everything is already synchronous.
                InjectThreadBarriers i{block_allocations, register_allocs};
                body = i(body);
            }

            debug(3) << "Injected synchronization:\n"
                     << body << "\n\n";

            ReplaceForWithIf f(block_size);
            body = f(body);

            debug(3) << "Replaced for with if:\n"
                     << body << "\n\n";

            // There is always a loop over the innermost thread dimension
            string thread_id = gpu_thread_name(0);
            // Add back in any register-level allocations
            body = register_allocs.rewrap(body, thread_id);
            body = For::make(thread_id, 0, block_size_x - 1, innermost_loop_type, op->partition_policy, op->device_api, body);

            // Rewrap the whole thing in other loops over threads
            for (int i = 1; i < block_size.threads_dimensions(); i++) {
                thread_id = gpu_thread_name(i);
                body = register_allocs.rewrap(body, thread_id);
                body = For::make(thread_id, 0, block_size.num_threads(i) - 1,
                                 ForType::GPUThread, op->partition_policy, op->device_api, body);
            }
            thread_id.clear();
            body = register_allocs.rewrap(body, thread_id);

            debug(3) << "Rewrapped in for loops:\n"
                     << body << "\n\n";

            // Add back in the shared allocations
            body = block_allocations.rewrap_block(body, block_size);
            debug(3) << "Add back in shared allocations:\n"
                     << body << "\n\n";

            return op->with(op->min, op->max, body);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    FuseGPUThreadLoopsSingleKernel(const ExtractBlockSize &bs,
                                   ExtractSharedAndHeapAllocations &sm)
        : block_size(bs), block_allocations(sm) {
    }
};

class FuseGPUThreadLoops : public IRMutator {
protected:
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        user_assert(!(op->for_type == ForType::GPUThread ||
                      op->for_type == ForType::GPULane))
            << "Loops over GPU thread variable: \"" << op->name
            << "\" is outside of any loop over a GPU block variable. "
            << "This schedule is malformed. There must be a GPU block "
            << "variable, and it must reordered to be outside all GPU "
            << "thread variables.\n";

        if (op->for_type == ForType::GPUBlock) {
            // Do the analysis of thread block size and shared memory
            // usage.
            ExtractBlockSize block_size;
            block_size(op);
            Stmt loop(op);

            ExtractSharedAndHeapAllocations block_allocations(op->device_api);
            loop = block_allocations(loop);

            debug(3) << "Pulled out shared allocations:\n"
                     << loop << "\n\n";

            // Mutate the inside of the kernel
            loop = FuseGPUThreadLoopsSingleKernel(block_size, block_allocations)(loop);

            loop = block_allocations.rewrap_kernel_launch(loop, block_size, op->device_api);

            return loop;
        } else {
            return IRMutator::visit(op);
        }
    }
};

class ZeroGPULoopMins : public IRMutator {
protected:
    bool in_non_glsl_gpu = false;
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        ScopedValue<bool> old_in_non_glsl_gpu(in_non_glsl_gpu);

        in_non_glsl_gpu = (in_non_glsl_gpu && op->device_api == DeviceAPI::None) ||
                          (op->device_api == DeviceAPI::CUDA) || (op->device_api == DeviceAPI::OpenCL) ||
                          (op->device_api == DeviceAPI::Metal) ||
                          (op->device_api == DeviceAPI::D3D12Compute) ||
                          (op->device_api == DeviceAPI::Vulkan);

        Stmt stmt = IRMutator::visit(op);
        if (is_gpu(op->for_type) && !is_const_zero(op->min)) {
            op = stmt.as<For>();
            internal_assert(op);
            Expr adjusted = Variable::make(Int(32), op->name) + op->min;
            Stmt body = substitute(op->name, adjusted, op->body);
            stmt = op->with(0, simplify(op->max - op->min), body);
        }
        return stmt;
    }

public:
    ZeroGPULoopMins() = default;
};

}  // namespace

// Also used by InjectImageIntrinsics
Stmt zero_gpu_loop_mins(const Stmt &s) {
    return ZeroGPULoopMins()(s);
}

namespace {

// Find the inner most GPU block of a statement.
class FindInnermostGPUBlock : public IRVisitor {
protected:
    using IRVisitor::visit;

    void visit(const For *op) override {
        if (op->for_type == ForType::GPUBlock) {
            // Set the last found GPU block to found_gpu_block.
            found_gpu_block = op;
        }
        IRVisitor::visit(op);
    }

public:
    const For *found_gpu_block = nullptr;
};

// Given a condition and a loop, add the condition
// to the loop body.
class AddConditionToALoop : public IRMutator {
protected:
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (op != loop) {
            return IRMutator::visit(op);
        }

        return op->with(op->min, op->max, IfThenElse::make(condition, op->body, Stmt()));
    }

public:
    AddConditionToALoop(const Expr &condition, const For *loop)
        : condition(condition), loop(loop) {
    }
    const Expr &condition;
    const For *loop;
};

// Push if statements between GPU blocks through all GPU blocks.
// Throw error if the if statement has an else clause.
class NormalizeIfStatements : public IRMutator {
protected:
    using IRMutator::visit;

    bool inside_gpu_blocks = false;

    Stmt visit(const For *op) override {
        if (op->for_type != ForType::GPUBlock) {
            return IRMutator::visit(op);
        }
        ScopedValue<bool> old_inside_gpu_blocks(inside_gpu_blocks, true);
        return IRMutator::visit(op);
    }

    Stmt visit(const IfThenElse *op) override {
        if (!inside_gpu_blocks) {
            return IRMutator::visit(op);
        }
        FindInnermostGPUBlock find;
        find(op);
        if (find.found_gpu_block != nullptr) {
            internal_assert(!op->else_case.defined()) << "Found an if statement with else case between two GPU blocks.\n";
            return AddConditionToALoop(op->condition, find.found_gpu_block)(op->then_case);
        }
        return IRMutator::visit(op);
    }
};

}  // namespace

// A prefetch of a region arrives as a serial loop of one-line prefetches,
// which every thread of the block would run identically. The lines are only
// hints, so deal them out across the block's threads instead: each thread
// takes every block-size'th line, and the last line stands in for the
// remainder, since prefetching it twice is free.
class DistributePrefetches : public IRMutator {
    using IRMutator::visit;

    vector<std::pair<string, Expr>> thread_loops;  // name, extent

    Stmt visit(const For *op) override {
        if (op->for_type == ForType::GPUThread || op->for_type == ForType::GPULane) {
            thread_loops.emplace_back(op->name, op->extent());
            Stmt s = IRMutator::visit(op);
            thread_loops.pop_back();
            return s;
        }
        Stmt body = op->body;
        vector<std::pair<string, Expr>> lets;
        while (const LetStmt *let = body.as<LetStmt>()) {
            lets.emplace_back(let->name, let->value);
            body = let->body;
        }
        const Evaluate *eval = body.as<Evaluate>();
        const Call *prefetch =
            eval ? Call::as_intrinsic(eval->value, {Call::prefetch}) : nullptr;
        if (op->for_type != ForType::Serial || !prefetch || thread_loops.empty()) {
            return IRMutator::visit(op);
        }
        Expr tid = make_zero(Int(32));
        Expr step = make_one(Int(32));
        for (const auto &t : thread_loops) {
            tid += Variable::make(Int(32), t.first) * step;
            step *= t.second;
        }
        Expr total = op->extent();
        Expr i = min(tid, total - 1) + op->min;
        for (size_t j = lets.size(); j > 0; j--) {
            i = Let::make(lets[j - 1].first, lets[j - 1].second, i);
        }
        Stmt s = substitute(op->name, Variable::make(Int(32), op->name + ".line"), body);
        s = LetStmt::make(op->name + ".line", simplify(i), s);
        for (size_t j = lets.size(); j > 0; j--) {
            s = LetStmt::make(lets[j - 1].first, lets[j - 1].second, s);
        }
        // Rounds that still have lines left keep the round-robin structure:
        // strides of the block size, starting past the first dealt round.
        // Usually an empty loop; a step's worth of lines rarely exceeds the
        // number of threads.
        Stmt strided;
        {
            string k = op->name + ".round";
            Expr kv = Variable::make(Int(32), k);
            Expr line = min(op->min + step + kv * step + tid, op->min + total - 1);
            Stmt body2 = substitute(op->name, Variable::make(Int(32), op->name + ".line"), op->body);
            body2 = LetStmt::make(op->name + ".line", simplify(line), body2);
            Expr rounds = (total - step + step - 1) / step;  // ceil((total-step)/step)
            strided = For::make(k, 0, simplify(rounds - 1), ForType::Serial,
                                op->partition_policy, op->device_api, body2);
            strided = IfThenElse::make(simplify(total > step), strided);
        }
        return Block::make(s, strided);
    }

public:
    using IRMutator::mutate;
};

Stmt fuse_gpu_thread_loops(Stmt s, const map<string, Function> &env) {
    // How far ahead of its consumer each Func's producer runs. The name the
    // stores carry is the realization name, which for a Func with a single
    // realization is the Func's own.
    map<string, int> slide_depths;
    for (const auto &p : env) {
        int depth = 0;
        for (const auto &sl : p.second.schedule().slide_levels()) {
            depth = std::max(depth, sl.depth);
        }
        if (depth > 0) {
            slide_depths[p.first] = depth;
        }
    }
    // NormalizeIfStatements pushes the predicates between GPU blocks
    // into the innermost GPU block. FuseGPUThreadLoops would then
    // merge the predicate into the merged GPU thread.
    s = NormalizeIfStatements()(s);
    // Must run before the allocations are packed together, because packing
    // them relies on there being only one kind of shared memory.
    s = MarkAsyncCopies(slide_depths).mutate(s);
    s = DistributePrefetches().mutate(s);
    s = FuseGPUThreadLoops()(s);
    s = ZeroGPULoopMins()(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
