#include <algorithm>
#include <cmath>
#include <utility>

#include "Bounds.h"
#include "CSE.h"
#include "CodeGen_GPU_Dev.h"
#include "CompilerLogger.h"
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

string thread_names[] = {"__thread_id_x", "__thread_id_y", "__thread_id_z", "__thread_id_w"};
string block_names[] = {"__block_id_x", "__block_id_y", "__block_id_z", "__block_id_w"};

class ExtractBlockSize : public IRVisitor {
    Expr block_extent[4], block_count[4];
    string block_var_name[4];

    using IRVisitor::visit;

    void found_thread_for(int dim, const string &name, const Expr &extent) {
        internal_assert(dim >= 0 && dim < 4);
        if (!block_extent[dim].defined()) {
            block_extent[dim] = extent;
        } else {
            block_extent[dim] = simplify(Max::make(extent, block_extent[dim]));
        }
    }

    void found_block_for(int dim, const string &name, Expr extent) {
        internal_assert(dim >= 0 && dim < 4);
        internal_assert(!block_count[dim].defined());
        block_count[dim] = std::move(extent);
        block_var_name[dim] = name;
    }

    void visit(const For *op) override {
        for (int i = 0; i < 4; i++) {
            if (ends_with(op->name, thread_names[i])) {
                found_thread_for(i, op->name, op->extent);
            } else if (ends_with(op->name, block_names[i])) {
                found_block_for(i, op->name, op->extent);
            }
        }

        IRVisitor::visit(op);

        Scope<Interval> scope;
        scope.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));
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
        for (int i = 0; i < 4; i++) {
            if (!block_count[i].defined()) {
                return i;
            }
        }
        return 4;
    }

    int threads_dimensions() const {
        for (int i = 0; i < 4; i++) {
            if (!block_extent[i].defined()) {
                return i;
            }
        }
        return 4;
    }

    Expr num_threads(int d) const {
        return block_extent[d];
    }

    Expr num_blocks(int d) const {
        return block_count[d];
    }

    Expr block_var(int d) const {
        return Variable::make(Int(32), block_var_name[d]);
    }

    Expr thread_var(int d) const {
        // Thread variables get canonical names
        return Variable::make(Int(32), "." + thread_names[d]);
    }
};

class NormalizeDimensionality : public IRMutator {
    using IRMutator::visit;

    const ExtractBlockSize &block_size;
    const DeviceAPI device_api;

    int depth = 0;
    int max_depth = 0;

    Stmt wrap(Stmt s) {
        if (depth != 0) {
            return mutate(s);
        }
        max_depth = 0;
        s = mutate(s);
        if (is_no_op(s)) {
            return s;
        }
        while (max_depth < block_size.threads_dimensions()) {
            string name = thread_names[max_depth];
            s = For::make("." + name, 0, 1, ForType::GPUThread, Partition::Never, device_api, s);
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
        : block_size(e), device_api(device_api) {
    }
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

            internal_assert(dim >= 0 && dim < block_size.threads_dimensions());

            Stmt body = mutate(op->body);

            Expr var = Variable::make(Int(32), "." + thread_names[dim]);
            body = substitute(op->name, var + op->min, body);

            if (equal(op->extent, block_size.num_threads(dim))) {
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
    ReplaceForWithIf(const ExtractBlockSize &e)
        : block_size(e) {
    }
};

class ExtractSharedAndHeapAllocations : public IRMutator {
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
            : name(alloc.name),
              widest_type(alloc.type),
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
            name += "_" + alloc.name;
        }

        // Only need to check the back of the vector since we always insert
        // the most recent allocation at the back.
        bool is_free(int stage) const {
            return group.back().liveness.max < stage;
        }

        string name;
        Type widest_type;
        Expr max_size;                   // In units of the widest type
        vector<SharedAllocation> group;  // Groups of allocs that should be coalesced together
        MemoryType memory_type;          // All allocations in the group have this memory type
    };

public:
    vector<SharedAllocation> allocations;

private:
    map<string, SharedAllocation *> shared;

    bool in_threads = false;

    int barrier_stage = 0;

    const DeviceAPI device_api;

    string thread_id_var_name, num_threads_var_name;

    bool may_merge_allocs_of_different_type;

    // A loop on the host used to compute the shared memory size
    Stmt host_side_preamble;

    void precompute_allocation_size(SharedAllocation &s) {
        Expr val = Load::make(Int(32), s.name + ".shared_size", 0,
                              Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
        Stmt update_size = Store::make(s.name + ".shared_size", max(s.size, val), 0,
                                       Parameter{}, const_true(), ModulusRemainder{});

        if (host_side_preamble.defined()) {
            host_side_preamble = Block::make(host_side_preamble, update_size);
        } else {
            host_side_preamble = update_size;
        }
        s.size_computed_on_host = true;
        s.size = Variable::make(Int(32), s.name + ".shared_size_var");
    }

    Stmt visit(const For *op) override {
        bool is_thread_loop = CodeGen_GPU_Dev::is_gpu_thread_var(op->name);
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
        scope.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));
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
                    if (get_compiler_logger()) {
                        get_compiler_logger()->record_non_monotonic_loop_var(op->name, s.size);
                    }
                    precompute_allocation_size(s);
                    break;
                case Monotonic::Increasing:
                    s.size = substitute(op->name, simplify(op->min + op->extent - 1), s.size);
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
                s.size *= op->extent;
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
                                           ForType::Serial, Partition::Never, DeviceAPI::None, host_side_preamble);
            if (old_preamble.defined()) {
                host_side_preamble = Block::make(old_preamble, host_side_preamble);
            }
        } else {
            host_side_preamble = old_preamble;
        }

        return For::make(op->name, new_min, new_extent,
                         op->for_type, op->partition_policy,
                         op->device_api, body);
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

    int alloc_node_counter = 0;

    Stmt visit(const Allocate *op) override {
        user_assert(!op->new_expr.defined())
            << "Allocate node inside GPU kernel has custom new expression.\n"
            << "(Memoization is not supported inside GPU kernels at present.)\n";

        bool fixed_size_thread_allocation = (op->constant_allocation_size() != 0) && in_threads;

        if ((fixed_size_thread_allocation &&
             op->memory_type != MemoryType::Heap &&
             op->memory_type != MemoryType::GPUShared &&
             op->memory_type != MemoryType::GPUTexture) ||
            op->memory_type == MemoryType::Register ||
            op->memory_type == MemoryType::Stack) {
            // These allocations go in register or local memory
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Auto ||
                    op->memory_type == MemoryType::GPUShared ||
                    op->memory_type == MemoryType::GPUTexture ||
                    op->memory_type == MemoryType::Heap)
            << "Allocation " << op->name << " must live in shared or heap memory, "
            << "but is scheduled to live in " << op->memory_type << " memory.\n";

        SharedAllocation alloc;
        alloc.name = op->name + "." + std::to_string(alloc_node_counter++);
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
            Expr predicate = mutate(op->predicate);
            Expr index = mutate_index(alloc, op->index);
            return Load::make(op->type, alloc->name,
                              index, op->image, op->param, predicate, op->alignment);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        auto it = shared.find(op->name);
        if (it != shared.end()) {
            SharedAllocation *alloc = it->second;
            alloc->liveness.max = barrier_stage;
            Expr predicate = mutate(op->predicate);
            Expr index = mutate_index(alloc, op->index);
            Expr value = mutate(op->value);
            return Store::make(alloc->name, value, index,
                               op->param, predicate, op->alignment);
        } else {
            return IRMutator::visit(op);
        }
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
            host_side_preamble = LetStmt::make(op->name, op->value, host_side_preamble);
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
            return LetStmt::make(op->name, value, body);
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
                    // Types must also match for OpenGLCompute
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
                    // Types must also match for OpenGLCompute
                    continue;
                }

                if (is_const(mem_allocs[free_spaces[i]].max_size)) {
                    const auto &candidate_group = mem_allocs[free_spaces[i]];
                    Expr size = alloc_size * alloc.type.bytes();
                    Expr dist = candidate_group.max_size * candidate_group.widest_type.bytes() - size;
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
    Stmt rewrap_block(Stmt s, const ExtractBlockSize &bs) {

        // Combine the allocations into groups that have disjoint
        // lifetimes, and then cluster the groups according to which
        // ones can share a single allocation. For cuda, opencl, and
        // similar we get one big combined allocation per memory
        // type. For vulkan, openglcompute and direct3d, we also separate by
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
            string name;
            Expr total_size = 0;
            Type widest_type;
            int number_of_allocs = 0;
            for (const auto &alloc : cluster) {
                number_of_allocs += alloc.group.size();
            }
            for (const auto &alloc : cluster) {
                if (name.empty()) {
                    widest_type = alloc.widest_type;
                    if (number_of_allocs > 1) {
                        name = "allocgroup__" + alloc.name;
                    } else {
                        name = alloc.name;
                    }
                } else {
                    if (alloc.widest_type.bytes() > widest_type.bytes()) {
                        widest_type = alloc.widest_type;
                    }
                    name += "__" + alloc.name;
                }
                int ratio = alloc.widest_type.bytes() / alloc_type.bytes();
                internal_assert(ratio != 0)
                    << "alloc_type should have been at most as wide as the widest type in group\n";
                total_size += alloc.max_size * ratio;
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

            // Make the allocation
            if (memory_type == MemoryType::Heap) {
                global_allocations.push_back(GlobalAllocation{name, total_size, alloc_type});
            } else {
                s = Allocate::make(name, alloc_type, memory_type,
                                   {total_size_var}, const_true(), s);
            }

            // Define a group offset for each group in the
            // cluster. The group offsets are in elements of
            // widest_type across the entire cluster. Using that,
            // define an individual offset for each allocation in the
            // group, using units of that allocation's type.
            for (int i = (int)(cluster.size()) - 1; i >= 0; i--) {
                Expr group_offset = Variable::make(Int(32), name + "." + std::to_string(i) + ".offset");

                for (const SharedAllocation &alloc : cluster[i].group) {
                    // Change units, as described above.
                    Expr offset = group_offset;
                    internal_assert(alloc.type.bytes() <= widest_type.bytes());
                    if (alloc.type.bytes() < widest_type.bytes()) {
                        offset *= (widest_type.bytes() / alloc.type.bytes());
                    }
                    offset = simplify(offset);

                    // Rewrite all loads and stores to point to the allocation
                    // cluster they belong to with the appropriate offset into it.
                    class RewriteGroupAccess : public IRMutator {
                        using IRMutator::visit;
                        Expr visit(const Load *op) override {
                            if (op->name == alloc_name) {
                                return Load::make(op->type, cluster_name, mutate(op->index) + offset,
                                                  op->image, op->param, mutate(op->predicate),
                                                  op->alignment);
                            } else {
                                return IRMutator::visit(op);
                            }
                        }

                        Stmt visit(const Store *op) override {
                            if (op->name == alloc_name) {
                                return Store::make(cluster_name, mutate(op->value), mutate(op->index) + offset,
                                                   op->param, mutate(op->predicate), op->alignment);
                            } else {
                                return IRMutator::visit(op);
                            }
                        }
                        const string &alloc_name;
                        const string &cluster_name;
                        const Expr &offset;

                    public:
                        RewriteGroupAccess(const string &alloc_name,
                                           const string &cluster_name,
                                           const Expr &offset)
                            : alloc_name(alloc_name), cluster_name(cluster_name), offset(offset) {
                        }
                    } rewriter{alloc.name, name, offset};
                    s = rewriter.mutate(s);
                }

                // Define the group offset in terms of the previous group in the cluster
                Expr offset;
                if (i > 0) {
                    // Build off the last offset
                    offset = Variable::make(Int(32), name + "." + std::to_string(i - 1) + ".offset");
                    int ratio = (widest_type.bytes() / cluster[i - 1].widest_type.bytes());
                    internal_assert(ratio != 0);
                    offset += simplify((cluster[i - 1].max_size + ratio - 1) / ratio);
                } else {
                    if (memory_type == MemoryType::Heap) {
                        // One slice of a larger global allocation
                        offset = get_block_id(bs) * total_size_var;
                    } else {
                        // Base address for shared memory is zero
                        offset = 0;
                    }
                }

                s = LetStmt::make(group_offset.as<Variable>()->name, simplify(offset), s);
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

    ExtractSharedAndHeapAllocations(DeviceAPI d)
        : device_api(d),
          thread_id_var_name(unique_name('t')),
          num_threads_var_name(unique_name('t')),
          may_merge_allocs_of_different_type(device_api != DeviceAPI::OpenGLCompute &&
                                             device_api != DeviceAPI::D3D12Compute &&
                                             device_api != DeviceAPI::Vulkan &&
                                             device_api != DeviceAPI::WebGPU) {
    }
};  // namespace Internal

// Pull out any allocate node outside of the innermost thread
// block. Should only be run after shared allocations have already
// been extracted.
class ExtractRegisterAllocations : public IRMutator {
    using IRMutator::visit;

    struct RegisterAllocation {
        string name;
        string loop_var;  // The nearest enclosing loop over threads. Empty if it's at block level.
        Type type;
        Expr size;
        MemoryType memory_type;  // Should be Auto, Stack, or Register
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
                    s.size = bounds_of_expr_in_scope(s.size, scope).max;
                }
            }

            // Add back on the allocations we set aside.
            if (!allocations.empty()) {
                allocations.insert(allocations.end(), old.begin(), old.end());
            } else {
                allocations.swap(old);
            }

            return For::make(op->name, mutate(op->min), mutate(op->extent), op->for_type, op->partition_policy, op->device_api, body);
        }
    }

    int alloc_node_counter = 0;
    Scope<string> alloc_renaming;

    Stmt visit(const Allocate *op) override {
        if (in_lane_loop) {
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Stack ||
                    op->memory_type == MemoryType::Register ||
                    op->memory_type == MemoryType::Heap ||
                    op->memory_type == MemoryType::Auto)
            << "Allocation " << op->name << " is scheduled inside a loop over GPU threads, so "
            << "it must live in stack memory, heap memory, or registers. "
            << "Shared allocations at this loop level are not yet supported.\n";

        ScopedBinding<int> p(register_allocations, op->name, 0);

        RegisterAllocation alloc;
        alloc.name = op->name + "." + std::to_string(alloc_node_counter++);
        alloc.type = op->type;
        alloc.size = 1;
        alloc.loop_var = loop_var;
        for (const auto &extent : op->extents) {
            alloc.size *= extent;
        }
        alloc.size = simplify(mutate(alloc.size));
        alloc.memory_type = op->memory_type;

        allocations.push_back(alloc);
        {
            ScopedBinding<string> bind(alloc_renaming, op->name, alloc.name);
            return mutate(op->body);
        }
    }

    Expr visit(const Load *op) override {
        string new_name = op->name;
        if (alloc_renaming.contains(op->name)) {
            new_name = alloc_renaming.get(op->name);
        }
        return Load::make(op->type, new_name, mutate(op->index),
                          op->image, op->param, mutate(op->predicate),
                          op->alignment);
    }

    Stmt visit(const Store *op) override {
        string new_name = op->name;
        if (alloc_renaming.contains(op->name)) {
            new_name = alloc_renaming.get(op->name);
        }
        return Store::make(new_name, mutate(op->value), mutate(op->index),
                           op->param, mutate(op->predicate), op->alignment);
    }

    template<typename ExprOrStmt, typename LetOrLetStmt>
    ExprOrStmt visit_let(const LetOrLetStmt *op) {
        ExprOrStmt body = op->body;

        body = mutate(op->body);
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
    bool in_threads = false, injected_barrier;

    using IRMutator::visit;

    const ExtractSharedAndHeapAllocations &block_allocs;
    const ExtractRegisterAllocations &register_allocs;

    std::set<std::string> shared_stores;
    std::set<std::string> device_stores;
    std::set<std::string> shared_loads;
    std::set<std::string> device_loads;

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
            return For::make(op->name, op->min, op->extent,
                             op->for_type, op->partition_policy, op->device_api, body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        debug(4) << "Encountered store to " << op->name << "\n";
        auto mem_type = memory_type_for_name(op->name);
        switch (mem_type) {
        case MemoryType::GPUShared:
            debug(4) << "   memory type is shared\n";
            shared_stores.insert(op->name);
            break;
        case MemoryType::Auto:
        case MemoryType::Heap:
        case MemoryType::GPUTexture:
            debug(4) << "   memory type is heap or auto\n";
            device_stores.insert(op->name);
            break;
        case MemoryType::Stack:
        case MemoryType::Register:
        case MemoryType::LockedCache:
        case MemoryType::VTCM:
        case MemoryType::AMXTile:
            break;
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        debug(4) << "Encountered load from " << op->name << "\n";
        auto mem_type = memory_type_for_name(op->name);
        switch (mem_type) {
        case MemoryType::GPUShared:
            debug(4) << "   memory type is shared\n";
            shared_loads.insert(op->name);
            break;
        case MemoryType::Auto:
        case MemoryType::Heap:
        case MemoryType::GPUTexture:
            debug(4) << "   memory type is heap or auto\n";
            device_loads.insert(op->name);
            break;
        case MemoryType::Stack:
        case MemoryType::Register:
        case MemoryType::LockedCache:
        case MemoryType::VTCM:
        case MemoryType::AMXTile:
            break;
        }

        return IRMutator::visit(op);
    }

    Stmt visit(const Block *op) override {
        if (!in_threads && op->rest.defined()) {
            // First, we record which loads from shared/device memory occur
            // in the rest block
            Stmt rest = mutate(op->rest);

            // Now, record which stores occur in the first stmt
            // of this block
            shared_stores.clear();
            device_stores.clear();
            Stmt first = mutate(op->first);

            // If there are any loads in the rest part that
            // load from something stored in first, insert the appropriate
            // fence type
            int mask = 0;
            for (const auto &st : shared_stores) {
                auto elem = shared_loads.find(st);
                if (elem != shared_loads.end()) {
                    mask |= CodeGen_GPU_Dev::MemoryFenceType::Shared;
                    break;
                }
            }
            for (const auto &st : device_stores) {
                auto elem = device_loads.find(st);
                if (elem != device_loads.end()) {
                    mask |= CodeGen_GPU_Dev::MemoryFenceType::Device;
                    break;
                }
            }
            injected_barrier = true;
            return Block::make({first, make_barrier(mask), rest});
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    InjectThreadBarriers(ExtractSharedAndHeapAllocations &sha, ExtractRegisterAllocations &ra)
        : block_allocs(sha),
          register_allocs(ra) {
    }
};

class FuseGPUThreadLoopsSingleKernel : public IRMutator {
    using IRMutator::visit;
    const ExtractBlockSize &block_size;
    ExtractSharedAndHeapAllocations &block_allocations;

    Stmt visit(const For *op) override {
        if (ends_with(op->name, ".__block_id_x")) {
            Stmt body = op->body;

            // This is the innermost loop over blocks.
            debug(3) << "Fusing thread block:\n"
                     << body << "\n\n";

            NormalizeDimensionality n(block_size, op->device_api);
            body = n.mutate(body);

            debug(3) << "Normalized dimensionality:\n"
                     << body << "\n\n";

            Expr block_size_x = block_size.threads_dimensions() ? block_size.num_threads(0) : 1;
            ExtractRegisterAllocations register_allocs;
            ForType innermost_loop_type = ForType::GPUThread;
            if (block_size.threads_dimensions()) {
                body = register_allocs.mutate(body);
                if (register_allocs.has_lane_loop) {
                    innermost_loop_type = ForType::GPULane;
                }
            }

            debug(3) << "Extracted register-level allocations:\n"
                     << body << "\n\n";

            if (register_allocs.has_thread_loop) {
                // If there's no loop over threads, everything is already synchronous.
                InjectThreadBarriers i{block_allocations, register_allocs};
                body = i.mutate(body);
            }

            debug(3) << "Injected synchronization:\n"
                     << body << "\n\n";

            ReplaceForWithIf f(block_size);
            body = f.mutate(body);

            debug(3) << "Replaced for with if:\n"
                     << body << "\n\n";

            // There is always a loop over thread_id_x
            string thread_id = "." + thread_names[0];
            // Add back in any register-level allocations
            body = register_allocs.rewrap(body, thread_id);
            body = For::make(thread_id, 0, block_size_x, innermost_loop_type, op->partition_policy, op->device_api, body);

            // Rewrap the whole thing in other loops over threads
            for (int i = 1; i < block_size.threads_dimensions(); i++) {
                thread_id = "." + thread_names[i];
                body = register_allocs.rewrap(body, thread_id);
                body = For::make("." + thread_names[i], 0, block_size.num_threads(i),
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

            if (body.same_as(op->body)) {
                return op;
            } else {
                return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, body);
            }
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
    using IRMutator::visit;

    Stmt visit(const For *op) override {
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

            ExtractSharedAndHeapAllocations block_allocations(op->device_api);
            loop = block_allocations.mutate(loop);

            debug(3) << "Pulled out shared allocations:\n"
                     << loop << "\n\n";

            // Mutate the inside of the kernel
            loop = FuseGPUThreadLoopsSingleKernel(block_size, block_allocations).mutate(loop);

            loop = block_allocations.rewrap_kernel_launch(loop, block_size, op->device_api);

            return loop;
        } else {
            return IRMutator::visit(op);
        }
    }
};

class ZeroGPULoopMins : public IRMutator {
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
        if (CodeGen_GPU_Dev::is_gpu_var(op->name) && !is_const_zero(op->min)) {
            op = stmt.as<For>();
            internal_assert(op);
            Expr adjusted = Variable::make(Int(32), op->name) + op->min;
            Stmt body = substitute(op->name, adjusted, op->body);
            stmt = For::make(op->name, 0, op->extent, op->for_type, op->partition_policy, op->device_api, body);
        }
        return stmt;
    }

public:
    ZeroGPULoopMins() = default;
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
            if (ends_with(op->name, block_names[4 - i])) {
                user_assert(i > gpu_block_depth)
                    << "Invalid schedule: Loop over " << op->name
                    << " cannot be inside of loop over " << innermost_block_var << "\n";
                user_assert(gpu_thread_depth == 0)
                    << "Invalid schedule: Loop over " << op->name
                    << " cannot be inside of loop over " << innermost_thread_var << "\n";
                innermost_block_var = op->name;
                gpu_block_depth = i;
            }
            if (ends_with(op->name, thread_names[4 - i])) {
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

}  // namespace

// Also used by InjectImageIntrinsics
Stmt zero_gpu_loop_mins(const Stmt &s) {
    return ZeroGPULoopMins().mutate(s);
}

namespace {

// Find the inner most GPU block of a statement.
class FindInnermostGPUBlock : public IRVisitor {
    using IRVisitor::visit;

    void visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_block_var(op->name)) {
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
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (op != loop) {
            return IRMutator::visit(op);
        }

        return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api,
                         IfThenElse::make(condition, op->body, Stmt()));
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
    using IRMutator::visit;

    bool inside_gpu_blocks = false;

    Stmt visit(const For *op) override {
        if (!CodeGen_GPU_Dev::is_gpu_block_var(op->name)) {
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
        op->accept(&find);
        if (find.found_gpu_block != nullptr) {
            internal_assert(!op->else_case.defined()) << "Found an if statement with else case between two GPU blocks.\n";
            return AddConditionToALoop(op->condition, find.found_gpu_block).mutate(op->then_case);
        }
        return IRMutator::visit(op);
    }
};

}  // namespace

Stmt fuse_gpu_thread_loops(Stmt s) {
    ValidateGPULoopNesting validate;
    s.accept(&validate);
    // NormalizeIfStatements pushes the predicates between GPU blocks
    // into the innermost GPU block. FuseGPUThreadLoops would then
    // merge the predicate into the merged GPU thread.
    s = NormalizeIfStatements().mutate(s);
    s = FuseGPUThreadLoops().mutate(s);
    s = ZeroGPULoopMins().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
