#include <algorithm>
#include <cmath>

#include "Bounds.h"
#include "CSE.h"
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
using std::sort;
using std::string;
using std::vector;

namespace {
string thread_names[] = {"__thread_id_x", "__thread_id_y", "__thread_id_z", "__thread_id_w"};
string block_names[] = {"__block_id_x", "__block_id_y", "__block_id_z", "__block_id_w"};
string shared_mem_name = "__shared";
}

class InjectThreadBarriers : public IRMutator {
    bool in_threads;

    using IRMutator::visit;

    Stmt barrier;

    Stmt visit(const For *op) override {
        ScopedValue<bool> old_in_threads(in_threads,
                                         (in_threads ||
                                          op->for_type == ForType::GPUThread ||
                                          op->for_type == ForType::GPULane));

        if (op->for_type == ForType::Serial) {
            Stmt body = mutate(op->body);
            // Serial for loops at the block level with internal
            // synchronization also need synchronization after each
            // loop iteration.
            if (!in_threads && !body.same_as(op->body)) {
                body = Block::make(body, barrier);
            }
            return For::make(op->name, op->min, op->extent,
                             op->for_type, op->device_api, body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Block *op) override {
        if (!in_threads && op->rest.defined()) {
            Stmt first = mutate(op->first);
            Stmt rest = mutate(op->rest);
            return Block::make(Block::make(first, barrier), rest);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    InjectThreadBarriers() : in_threads(false) {
        barrier =
            Evaluate::make(Call::make(Int(32), Call::gpu_thread_barrier,
                                      vector<Expr>(), Call::Intrinsic));
    }
};


class ExtractBlockSize : public IRVisitor {
    Expr block_extent[4];

    using IRVisitor::visit;

    void found_for(int dim, Expr extent) {
        internal_assert(dim >= 0 && dim < 4);
        if (!block_extent[dim].defined()) {
            block_extent[dim] = extent;
        } else {
            block_extent[dim] = simplify(Max::make(extent, block_extent[dim]));
        }
    }

    void visit(const For *op) override {
        for (int i = 0; i < 4; i++) {
            if (ends_with(op->name, thread_names[i])) {
                found_for(i, op->extent);
            }
        }

        IRVisitor::visit(op);

        Scope<Interval> scope;
        scope.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));
        for (int i = 0; i < 4; i++) {
            if (block_extent[i].defined() &&
                expr_uses_var(block_extent[i], op->name)) {
                block_extent[i] = simplify(common_subexpression_elimination(block_extent[i]));
                if (is_monotonic(block_extent[i], op->name) == Monotonic::Unknown) {
                    user_warning << "Thread id " << i
                                 << " has a size that is non-monontonic in the gpu block variable " << op->name
                                 << ": " << block_extent[i] << "\n";
                }
                block_extent[i] = simplify(bounds_of_expr_in_scope(block_extent[i], scope).max);
            }
        }
    }

    void visit(const LetStmt *op) override {
        IRVisitor::visit(op);
        for (int i = 0; i < 4; i++) {
            if (block_extent[i].defined() &&
                expr_uses_var(block_extent[i], op->name)) {
                block_extent[i] = simplify(Let::make(op->name, op->value, block_extent[i]));
            }
        }
    }

public:
    int dimensions() const {
        for (int i = 0; i < 4; i++) {
            if (!block_extent[i].defined()) {
                return i;
            }
        }
        return 4;
    }

    Expr extent(int d) const {
        return block_extent[d];
    }
};

class NormalizeDimensionality : public IRMutator {
    using IRMutator::visit;

    const ExtractBlockSize &block_size;
    const DeviceAPI device_api;

    int depth;
    int max_depth;

    Stmt wrap(Stmt s) {
        if (depth != 0) {
            return mutate(s);
        }
        max_depth = 0;
        s = mutate(s);
        if (is_no_op(s)) {
            return s;
        }
        while (max_depth < block_size.dimensions()) {
            string name = thread_names[max_depth];
            s = For::make("." + name, 0, 1, ForType::GPUThread, device_api, s);
            max_depth++;
        }
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
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            depth++;
            if (depth > max_depth) {
                max_depth = depth;
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
      : block_size(e), device_api(device_api), depth(0), max_depth(0) {}
};

class ReplaceForWithIf : public IRMutator {
    using IRMutator::visit;

    const ExtractBlockSize &block_size;

    Stmt visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            int dim;
            for (dim = 0; dim < 4; dim++) {
                if (ends_with(op->name, thread_names[dim])) {
                    break;
                }
            }

            internal_assert(dim >= 0 && dim < block_size.dimensions());

            Stmt body = mutate(op->body);

            Expr var = Variable::make(Int(32), "." + thread_names[dim]);
            body = substitute(op->name, var + op->min, body);

            if (equal(op->extent, block_size.extent(dim))) {
                return body;
            } else {
                Expr cond = var < op->extent;
                return IfThenElse::make(cond, body, Stmt());
            }
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    ReplaceForWithIf(const ExtractBlockSize &e) : block_size(e) {}
};

class ExtractSharedAllocations : public IRMutator {
    using IRMutator::visit;

    struct IntInterval {
        IntInterval() : IntInterval(0, 0) {}
        IntInterval(int min, int max) : min(min), max(max) {}
        int min;
        int max;
    };

    struct SharedAllocation {
        string name;
        Type type;
        Expr size;
        IntInterval liveness; // Start and end of the barrier stage at which this allocation is used.
        bool size_computed_on_host = false;
    };

    struct AllocGroup {
        AllocGroup() {}
        AllocGroup(const SharedAllocation &alloc) : max_type_bytes(alloc.type.bytes()) {
            max_size_bytes = simplify(alloc.type.bytes() * alloc.size);
            group.push_back(alloc);
        }

        void insert(const SharedAllocation &alloc) {
            max_type_bytes = std::max(max_type_bytes, alloc.type.bytes());
            max_size_bytes = simplify(max(max_size_bytes, simplify(alloc.size * alloc.type.bytes())));
            group.push_back(alloc);
        }

        // Only need to check the back of the vector since we always insert
        // the most recent allocation at the back.
        bool is_free(int stage) const {
            return group.back().liveness.max < stage;
        }

        int max_type_bytes;
        Expr max_size_bytes; // In bytes
        vector<SharedAllocation> group; // Groups of allocs that should be coalesced together
    };

    vector<SharedAllocation> allocations;
    map<string, IntInterval> shared;

    bool in_threads;

    int barrier_stage;

    const DeviceAPI device_api;

    Stmt host_side_preamble;

    Stmt visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            bool old = in_threads;
            in_threads = true;
            Stmt stmt = IRMutator::visit(op);
            in_threads = old;
            return stmt;
        } else {
            // Set aside the allocations we've found so far.
            vector<SharedAllocation> old;
            old.swap(allocations);
            Stmt old_preamble = host_side_preamble;
            host_side_preamble = Stmt();

            // Find allocations inside the loop body
            Stmt body = mutate(op->body);

            // Expand any new shared allocations found in the body using the loop bounds.
            Scope<Interval> scope;
            scope.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));

            // Expand the inner allocations using the loop bounds.
            for (SharedAllocation &s : allocations) {
                if (expr_uses_var(s.size, op->name) && !s.size_computed_on_host) {
                    s.size = simplify(common_subexpression_elimination(s.size));
                    // It's worth working extra hard to remove any
                    // repeated dependence on the block var
                    s.size = solve_expression(s.size, op->name).result;
                    s.size = simplify(common_subexpression_elimination(s.size));
                    auto result = is_monotonic(s.size, op->name);
                    if (result == Monotonic::Unknown) {
                        user_warning << "Shared allocation for " << s.name
                                     << " has a size that is non-monontonic in the gpu block variable " << op->name
                                     << ": " << s.size << "\n";
                        s.size_computed_on_host = true;
                        Expr val = Load::make(Int(32), s.name + ".shared_size", 0,
                                              Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
                        Stmt update_size = Store::make(s.name + ".shared_size", max(s.size, val), 0,
                                                       Parameter{}, const_true(), ModulusRemainder{});

                        if (host_side_preamble.defined()) {
                            host_side_preamble = Block::make(host_side_preamble, update_size);
                        } else {
                            host_side_preamble = update_size;
                        }
                    } else {
                        s.size = bounds_of_expr_in_scope(s.size, scope).max;
                    }
                }
            }

            // Add back on the allocations we set aside.
            if (!allocations.empty()) {
                allocations.insert(allocations.end(), old.begin(), old.end());
            } else {
                allocations.swap(old);
            }

            Expr new_min = mutate(op->min);
            Expr new_extent = mutate(op->extent);

            if (host_side_preamble.defined()) {
                string loop_name = unique_name('t');
                Expr v = Variable::make(Int(32), loop_name);
                host_side_preamble = substitute(op->name, v, host_side_preamble);
                host_side_preamble = For::make(loop_name, new_min, new_extent,
                                               ForType::Serial, DeviceAPI::None, host_side_preamble);
                if (old_preamble.defined()) {
                    host_side_preamble = Block::make(old_preamble, host_side_preamble);
                }
            } else {
                host_side_preamble = old_preamble;
            }

            return For::make(op->name, new_min, new_extent, op->for_type, op->device_api, body);
        }
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

    Stmt visit(const Allocate *op) override {
        user_assert(!op->new_expr.defined()) << "Allocate node inside GPU kernel has custom new expression.\n" <<
            "(Memoization is not supported inside GPU kernels at present.)\n";

        if (in_threads ||
            op->memory_type == MemoryType::Stack ||
            op->memory_type == MemoryType::Register) {
            // TODO: Support shared allocations inside loops over threads by giving each loop iteration a unique slice.
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Auto ||
                    op->memory_type == MemoryType::GPUShared)
            << "Allocation " << op->name << " must live in shared memory, "
            << "but is scheduled to live in " << op->memory_type << " memory.\n";

        shared.emplace(op->name, IntInterval(barrier_stage, barrier_stage));
        Stmt stmt = IRMutator::visit(op);
        op = stmt.as<Allocate>();
        internal_assert(op);

        SharedAllocation alloc;
        alloc.name = op->name;
        alloc.type = op->type;
        alloc.liveness = shared[op->name];
        alloc.size = 1;
        for (size_t i = 0; i < op->extents.size(); i++) {
            alloc.size *= op->extents[i];
        }
        alloc.size = simplify(alloc.size);
        allocations.push_back(alloc);
        shared.erase(op->name);
        return op->body;
    }

    Expr visit(const Load *op) override {
        if (shared.count(op->name)) {
            Expr predicate = mutate(op->predicate);
            Expr index = mutate(op->index);
            shared[op->name].max = barrier_stage;
            if (device_api == DeviceAPI::OpenGLCompute) {
                return Load::make(op->type, shared_mem_name + "_" + op->name,
                                  index, op->image, op->param, predicate, op->alignment);
            } else {
                Expr base = Variable::make(Int(32), op->name + ".shared_offset");
                return Load::make(op->type, shared_mem_name, base + index,
                                  op->image, op->param, predicate, ModulusRemainder());
            }

        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (shared.count(op->name)) {
            shared[op->name].max = barrier_stage;
            Expr predicate = mutate(op->predicate);
            Expr index = mutate(op->index);
            Expr value = mutate(op->value);
            if (device_api == DeviceAPI::OpenGLCompute) {
                return Store::make(shared_mem_name + "_" + op->name, value, index,
                                   op->param, predicate, op->alignment);
            } else {
                Expr base = Variable::make(Int(32), op->name + ".shared_offset");
                return Store::make(shared_mem_name, value, base + index, op->param, predicate, ModulusRemainder());
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (in_threads) {
            return IRMutator::visit(op);
        }

        Expr value = mutate(op->value);
        Stmt body = mutate(op->body);

        for (SharedAllocation &s : allocations) {
            if (expr_uses_var(s.size, op->name) && !s.size_computed_on_host) {
                s.size = simplify(Let::make(op->name, op->value, s.size));
            }
        }

        if (host_side_preamble.defined() &&
            stmt_uses_var(host_side_preamble, op->name)) {
            host_side_preamble = LetStmt::make(op->name, op->value, host_side_preamble);
        }

        if (op->body.same_as(body) && value.same_as(op->value)) {
            return op;
        } else {
            return LetStmt::make(op->name, value, body);
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

    // Return index to free_spaces where 'alloc' should be coalesced. Return -1
    // if there isn't any.
    int find_best_fit(const vector<AllocGroup>& mem_allocs,
                      const vector<int>& free_spaces,
                      const SharedAllocation& alloc, int stage) {
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

        if (!is_const(alloc_size)) { // dynamic-sized alloc
            for (int i = free_spaces.size() - 1; i >= 0; --i) {
                internal_assert(free_spaces[i] >= 0 && free_spaces[i] < (int)mem_allocs.size());
                internal_assert(mem_allocs[free_spaces[i]].is_free(stage));

                if (!is_const(mem_allocs[free_spaces[i]].max_size_bytes)) {
                    return i;
                } else if (free_idx == -1) {
                    free_idx = i;
                }
            }
        } else { // constant-sized alloc
            int64_t diff = -1;
            for (int i = free_spaces.size() - 1; i >= 0; --i) {
                internal_assert(free_spaces[i] >= 0 && free_spaces[i] < (int)mem_allocs.size());
                internal_assert(mem_allocs[free_spaces[i]].is_free(stage));

                if (is_const(mem_allocs[free_spaces[i]].max_size_bytes)) {
                    Expr size = alloc_size * alloc.type.bytes();
                    Expr dist = mem_allocs[free_spaces[i]].max_size_bytes - size;
                    const int64_t *current_diff = as_const_int(simplify(dist));
                    internal_assert(current_diff != nullptr);
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
        // Sort based on the ascending order of the min liveness stage; if equal,
        // sort based on the ascending order of the max liveness stage.
        sort(allocations.begin(), allocations.end(),
            [](const SharedAllocation &lhs, const SharedAllocation &rhs){
                if (lhs.liveness.min < rhs.liveness.min) {
                    return true;
                } else if (lhs.liveness.min == rhs.liveness.min) {
                    return lhs.liveness.max < rhs.liveness.max;
                }
                return false;
            }
        );

        vector<AllocGroup> mem_allocs;
        vector<int> free_spaces; // Contains index to free spaces in mem_allocs
        int start_idx = 0;

        for (int stage = 0; stage < barrier_stage; ++stage) {
            for (int i = start_idx; i < (int)allocations.size(); ++i) {
                if (allocations[i].liveness.min > stage) {
                    break;
                } else if (allocations[i].liveness.min == stage) { // Allocate
                    int free_idx = find_best_fit(mem_allocs, free_spaces, allocations[i], stage);
                    if (free_idx != -1) {
                        mem_allocs[free_spaces[free_idx]].insert(allocations[i]);
                        free_spaces.erase(free_spaces.begin() + free_idx);
                    } else {
                        mem_allocs.push_back(AllocGroup(allocations[i]));
                    }
                } else if (allocations[i].liveness.max == stage - 1) { // Free
                    int free_idx = -1;
                    for (int j = 0; j < (int)mem_allocs.size(); ++j) { // Find the index of the space to free
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

public:
    Stmt rewrap(Stmt s) {

        if (device_api == DeviceAPI::OpenGLCompute) {

            // Individual shared allocations.
            for (SharedAllocation alloc : allocations) {
                s = Allocate::make(shared_mem_name + "_" + alloc.name,
                                   alloc.type, MemoryType::GPUShared,
                                   {alloc.size}, const_true(), s);
            }
        } else {
            // One big combined shared allocation.

            vector<AllocGroup> mem_allocs = allocate_funcs(allocations);

            // Sort the allocations by the max size in bytes of the primitive
            // types in the group. Because the type sizes are then decreasing powers of
            // two, doing this guarantees that all allocations are aligned
            // to then element type as long as the original one is aligned
            // to the widest type.
            sort(mem_allocs.begin(), mem_allocs.end(),
                [](const AllocGroup &lhs, const AllocGroup &rhs){
                    return lhs.max_type_bytes > rhs.max_type_bytes;
                }
            );

            SharedAllocation sentinel;
            sentinel.name = "sentinel";
            sentinel.type = UInt(8);
            sentinel.size = 0;
            mem_allocs.push_back(AllocGroup(sentinel));

            // Add a dummy allocation at the end to get the total size
            Expr total_size = Variable::make(Int(32), "group_" + std::to_string(mem_allocs.size()-1) + ".shared_offset");
            s = Allocate::make(shared_mem_name, UInt(8), MemoryType::GPUShared,
                               {total_size}, const_true(), s);

            // Define an offset for each allocation. The offsets are in
            // elements, not bytes, so that the stores and loads can use
            // them directly.
            for (int i = (int)(mem_allocs.size()) - 1; i >= 0; i--) {
                Expr group_offset = Variable::make(Int(32), "group_" + std::to_string(i) + ".shared_offset");

                for (SharedAllocation &alloc : mem_allocs[i].group) {
                    int new_elem_size = alloc.type.bytes();
                    Expr offset = (group_offset / new_elem_size);
                    s = LetStmt::make(alloc.name + ".shared_offset", simplify(offset), s);
                }

                Expr offset = 0;
                if (i > 0) {
                    offset = Variable::make(Int(32), "group_" + std::to_string(i-1) + ".shared_offset");
                    int new_elem_size = mem_allocs[i].max_type_bytes;
                    offset += (((mem_allocs[i-1].max_size_bytes + new_elem_size - 1)/new_elem_size)*new_elem_size);
                }
                s = LetStmt::make("group_" + std::to_string(i) + ".shared_offset", simplify(offset), s);
            }
        }

        return s;
    }

    Stmt compute_shared_memory_sizes_on_host(Stmt result) {
        if (!host_side_preamble.defined()) return result;

        // Make all the let stmts that define the size vars
        for (auto &alloc : allocations) {
            if (alloc.size_computed_on_host) {
                string alloc_name = alloc.name + ".shared_size";
                string var_name = alloc.name + ".shared_size_var";
                Expr val = Load::make(Int(32), alloc_name, 0,
                                      Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
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
                                        Parameter{}, const_true(), ModulusRemainder{});
                result = Block::make(init, result);
                result = Allocate::make(alloc_name, Int(32), MemoryType::Stack, {1}, const_true(), result);
            }
        }

        return result;
    }

    ExtractSharedAllocations(DeviceAPI d) : in_threads(false), barrier_stage(0), device_api(d) {}
};


// Pull out any allocate node outside of the innermost thread
// block. Should only be run after shared allocations have already
// been extracted.
class ExtractRegisterAllocations : public IRMutator {
    using IRMutator::visit;

    struct RegisterAllocation {
        string name;
        string loop_var; // The nearest enclosing loop over threads. Empty if it's at block level.
        Type type;
        Expr size;
        MemoryType memory_type; // Should be Auto, Stack, or Register
    };

    bool in_lane_loop = false;

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
            scope.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));

            // Expand the inner allocations using the loop bounds.
            for (RegisterAllocation &s : allocations) {
                if (expr_uses_var(s.size, op->name)) {
                    s.size = simplify(common_subexpression_elimination(s.size));
                    if (is_monotonic(s.size, op->name) == Monotonic::Unknown) {
                        user_warning << "Register allocation for " << s.name
                                     << " has a size that is non-monontonic in the gpu block variable " << op->name
                                     << ": " << s.size << "\n";
                    }
                    s.size = bounds_of_expr_in_scope(s.size, scope).max;
                }
            }

            // Add back on the allocations we set aside.
            if (!allocations.empty()) {
                allocations.insert(allocations.end(), old.begin(), old.end());
            } else {
                allocations.swap(old);
            }

            return For::make(op->name, mutate(op->min), mutate(op->extent), op->for_type, op->device_api, body);
        }
    }

    Stmt visit(const Allocate *op) override {
        if (in_lane_loop) {
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Stack ||
                    op->memory_type == MemoryType::Register ||
                    op->memory_type == MemoryType::Auto)
            << "Allocation " << op->name << " is scheduled inside a loop over GPU threads, so "
            << "it must live in stack memory or registers. "
            << "Shared allocations at this loop level are not yet supported.\n";

        ScopedBinding<int> p(register_allocations, op->name, 0);

        RegisterAllocation alloc;
        alloc.name = op->name;
        alloc.type = op->type;
        alloc.size = 1;
        alloc.loop_var = loop_var;
        for (size_t i = 0; i < op->extents.size(); i++) {
            alloc.size *= op->extents[i];
        }
        alloc.size = simplify(alloc.size);
        alloc.memory_type = op->memory_type;

        allocations.push_back(alloc);
        return mutate(op->body);
    }

    template<typename ExprOrStmt, typename LetOrLetStmt>
    ExprOrStmt visit_let(const LetOrLetStmt *op) {
        ExprOrStmt body = op->body;

        body = mutate(op->body);

        for (RegisterAllocation &s : allocations) {
            if (expr_uses_var(s.size, op->name)) {
                s.size = simplify(Let::make(op->name, op->value, s.size));
            }
        }

        if (op->body.same_as(body)) {
            return op;
        } else {
            return LetOrLetStmt::make(op->name, op->value, body);
        }
    }

    Expr visit(const Let *op) override {
        return visit_let<Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }


    Scope<int> register_allocations;
    string loop_var;

public:
    vector<RegisterAllocation> allocations;

    Stmt rewrap(Stmt body, const string &loop_var) {
        for (RegisterAllocation &alloc : allocations) {
            if ((!loop_var.empty() && ends_with(alloc.loop_var, loop_var)) |
                (loop_var.empty() && alloc.loop_var.empty())) {
                body = Allocate::make(alloc.name, alloc.type, alloc.memory_type, {alloc.size}, const_true(), body);
            }
        }
        return body;
    }

    bool has_lane_loop = false;
    bool has_thread_loop = false;
};

class FuseGPUThreadLoopsSingleKernel : public IRMutator {
    using IRMutator::visit;
    const ExtractBlockSize &block_size;
    ExtractSharedAllocations &shared_mem;

    Stmt visit(const For *op) override {
        if (ends_with(op->name, ".__block_id_x")) {
            Stmt body = op->body;

            // This is the innermost loop over blocks.
            debug(3) << "Fusing thread block:\n" << body << "\n\n";

            NormalizeDimensionality n(block_size, op->device_api);
            body = n.mutate(body);

            debug(3) << "Normalized dimensionality:\n" << body << "\n\n";

            Expr block_size_x = block_size.dimensions() ? block_size.extent(0) : 1;
            ExtractRegisterAllocations register_allocs;
            ForType innermost_loop_type = ForType::GPUThread;
            if (block_size.dimensions()) {
                body = register_allocs.mutate(body);
                if (register_allocs.has_lane_loop) {
                    innermost_loop_type = ForType::GPULane;
                }
            }

            debug(3) << "Extracted register-level allocations:\n" << body << "\n\n";

            if (register_allocs.has_thread_loop) {
                // If there's no loop over threads, everything is already synchronous.
                InjectThreadBarriers i;
                body = i.mutate(body);
            }

            debug(3) << "Injected synchronization:\n" << body << "\n\n";

            ReplaceForWithIf f(block_size);
            body = f.mutate(body);

            debug(3) << "Replaced for with if:\n" << body << "\n\n";

            // There is always a loop over thread_id_x
            string thread_id = "." + thread_names[0];
            // Add back in any register-level allocations
            body = register_allocs.rewrap(body, thread_id);
            body = For::make(thread_id, 0, block_size_x, innermost_loop_type, op->device_api, body);

            // Rewrap the whole thing in other loops over threads
            for (int i = 1; i < block_size.dimensions(); i++) {
                thread_id = "." + thread_names[i];
                body = register_allocs.rewrap(body, thread_id);
                body = For::make("." + thread_names[i], 0, block_size.extent(i), ForType::GPUThread, op->device_api, body);
            }
            thread_id.clear();
            body = register_allocs.rewrap(body, thread_id);

            debug(3) << "Rewrapped in for loops:\n" << body << "\n\n";

            // Add back in the shared allocations
            body = shared_mem.rewrap(body);
            debug(3) << "Add back in shared allocations:\n" << body << "\n\n";

            if (body.same_as(op->body)) {
                return op;
            } else {
                return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
            }
        } else {
            return IRMutator::visit(op);
        }

    }

public:
    FuseGPUThreadLoopsSingleKernel(const ExtractBlockSize &bs,
                                   ExtractSharedAllocations &sm) :
        block_size(bs), shared_mem(sm) {}

};

class FuseGPUThreadLoops : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (op->device_api == DeviceAPI::GLSL) {
            return op;
        }

        user_assert(!(CodeGen_GPU_Dev::is_gpu_thread_var(op->name)))
            << "Loops over GPU thread variable: \"" << op->name
            << "\" is outside of any loop over a GPU block variable. "
            << "This schedule is malformed. There must be a GPU block "
            << "variable, and it must reordered to be outside all GPU "
            << "thread variables.\n";

        if (CodeGen_GPU_Dev::is_gpu_block_var(op->name)) {
            // Do the analysis of thread block size and shared memory
            // usage.
            ExtractBlockSize block_size;
            Stmt loop = Stmt(op);
            loop.accept(&block_size);

            ExtractSharedAllocations shared_mem(op->device_api);
            loop = shared_mem.mutate(loop);

            // Prepend the code that computes any shared memory sizes
            // that need host-side computation.
            loop = shared_mem.compute_shared_memory_sizes_on_host(loop);

            debug(3) << "Pulled out shared allocations:\n" << loop << "\n\n";

            // Mutate the inside of the kernel
            loop = FuseGPUThreadLoopsSingleKernel(block_size, shared_mem).mutate(loop);

            return loop;
        } else {
            return IRMutator::visit(op);
        }
    }
};

class ZeroGPULoopMins : public IRMutator {
    bool in_non_glsl_gpu;
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        ScopedValue<bool> old_in_non_glsl_gpu(in_non_glsl_gpu);

        in_non_glsl_gpu = (in_non_glsl_gpu && op->device_api == DeviceAPI::None) ||
          (op->device_api == DeviceAPI::CUDA) || (op->device_api == DeviceAPI::OpenCL) ||
          (op->device_api == DeviceAPI::Metal) ||
          (op->device_api == DeviceAPI::D3D12Compute);

        Stmt stmt = IRMutator::visit(op);
        if (CodeGen_GPU_Dev::is_gpu_var(op->name) && !is_zero(op->min)) {
            op = stmt.as<For>();
            internal_assert(op);
            Expr adjusted = Variable::make(Int(32), op->name) + op->min;
            Stmt body = substitute(op->name, adjusted, op->body);
            stmt = For::make(op->name, 0, op->extent, op->for_type, op->device_api, body);
        }
        return stmt;
    }

public:
    ZeroGPULoopMins() : in_non_glsl_gpu(false) { }
};

class ValidateGPULoopNesting : public IRVisitor {
    int gpu_block_depth = 0, gpu_thread_depth = 0;
    string innermost_block_var, innermost_thread_var;

    using IRVisitor::visit;

    void visit(const For *op) override {
        ScopedValue<string> old_innermost_block_var(innermost_block_var);
        ScopedValue<string> old_innermost_thread_var(innermost_thread_var);
        ScopedValue<int> old_gpu_block_depth(gpu_block_depth);
        ScopedValue<int> old_gpu_thread_depth(gpu_thread_depth);

        for (int i = 1; i <= 4; i++) {
            if (ends_with(op->name, block_names[4-i])) {
                user_assert(i > gpu_block_depth)
                    << "Invalid schedule: Loop over " << op->name
                    << " cannot be inside of loop over " << innermost_block_var << "\n";
                user_assert(gpu_thread_depth == 0)
                    << "Invalid schedule: Loop over " << op->name
                    << " cannot be inside of loop over " << innermost_thread_var << "\n";
                innermost_block_var = op->name;
                gpu_block_depth = i;
            }
            if (ends_with(op->name, thread_names[4-i])) {
                user_assert(i > gpu_thread_depth)
                    << "Invalid schedule: Loop over " << op->name
                    << " cannot be inside of loop over " << innermost_thread_var << "\n";
                user_assert(gpu_block_depth > 0)
                    << "Invalid schedule: Loop over " << op->name
                    << " must be inside a loop over gpu blocks\n";
                innermost_thread_var = op->name;
                gpu_thread_depth = i;
            }
        }
        IRVisitor::visit(op);
    }
};

// Also used by InjectImageIntrinsics
Stmt zero_gpu_loop_mins(Stmt s) {
    return ZeroGPULoopMins().mutate(s);
}

Stmt fuse_gpu_thread_loops(Stmt s) {
    ValidateGPULoopNesting validate;
    s.accept(&validate);
    s = FuseGPUThreadLoops().mutate(s);
    s = ZeroGPULoopMins().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
