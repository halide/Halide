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
#include "Simplify.h"
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
}  // namespace

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
    InjectThreadBarriers()
        : in_threads(false) {
        barrier =
            Evaluate::make(Call::make(Int(32), Call::gpu_thread_barrier,
                                      vector<Expr>(), Call::Intrinsic));
    }
};

class ExtractBlockSize : public IRVisitor {
    Expr block_extent[4], block_count[4];
    string block_var_name[4];

    using IRVisitor::visit;

    void found_thread_for(int dim, const string &name, Expr extent) {
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
        block_count[dim] = extent;
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
        for (int i = 0; i < 4; i++) {
            if (block_extent[i].defined() &&
                expr_uses_var(block_extent[i], op->name)) {
                block_extent[i] = simplify(common_subexpression_elimination(block_extent[i]));
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
        while (max_depth < block_size.threads_dimensions()) {
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
        : block_size(e), device_api(device_api), depth(0), max_depth(0) {
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
    };

    struct AllocGroup {
        AllocGroup() = default;
        AllocGroup(const SharedAllocation &alloc)
            : max_type_bytes(alloc.type.bytes()),
              max_size_bytes(simplify(alloc.type.bytes() * alloc.size)),
              memory_type(alloc.memory_type) {
            group.push_back(alloc);
        }

        void insert(const SharedAllocation &alloc) {
            internal_assert(alloc.memory_type == memory_type);
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
        Expr max_size_bytes;             // In bytes
        vector<SharedAllocation> group;  // Groups of allocs that should be coalesced together
        MemoryType memory_type;          // All allocations in the group have this memory type
    };

    vector<SharedAllocation> allocations;
    map<string, SharedAllocation *> shared;

    bool in_threads;

    int barrier_stage;

    Expr heap_bytes_per_block;

    const DeviceAPI device_api;

    string thread_id_var_name, num_threads_var_name, heap_name;

    Stmt visit(const For *op) override {
        bool is_thread_loop = CodeGen_GPU_Dev::is_gpu_thread_var(op->name);
        ScopedValue<bool> old_in_threads(in_threads, in_threads || is_thread_loop);

        // Set aside the allocations we've found so far.
        vector<SharedAllocation> old;
        old.swap(allocations);

        // Find allocations inside the loop body
        Stmt body = mutate(op->body);

        // Expand any new shared allocations found in the body using the loop bounds.
        Scope<Interval> scope;
        scope.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));
        for (SharedAllocation &s : allocations) {
            // If the size depends on the loop variable, take the max
            // over all loop iterations
            if (expr_uses_var(s.size, op->name)) {
                auto interval_bounds = bounds_of_expr_in_scope(s.size, scope);
                user_assert(interval_bounds.has_upper_bound())
                    << "Couldn't infer bounds for " << s.name << " shared memory allocation\n";
                s.size = interval_bounds.max;
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

        return For::make(op->name, mutate(op->min), mutate(op->extent), op->for_type, op->device_api, body);
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

    int alloc_node_counter = 0;

    Stmt visit(const Allocate *op) override {
        user_assert(!op->new_expr.defined())
            << "Allocate node inside GPU kernel has custom new expression.\n"
            << "(Memoization is not supported inside GPU kernels at present.)\n";

        bool fixed_size_thread_allocation = (op->constant_allocation_size() != 0) && in_threads;

        if ((fixed_size_thread_allocation &&
             op->memory_type != MemoryType::Heap &&
             op->memory_type != MemoryType::GPUShared) ||
            op->memory_type == MemoryType::Register ||
            op->memory_type == MemoryType::Stack) {
            // These allocations go in register or local memory
            return IRMutator::visit(op);
        }

        user_assert(op->memory_type == MemoryType::Auto ||
                    op->memory_type == MemoryType::GPUShared ||
                    op->memory_type == MemoryType::Heap)
            << "Allocation " << op->name << " must live in shared or heap memory, "
            << "but is scheduled to live in " << op->memory_type << " memory.\n";

        SharedAllocation alloc;
        alloc.name = op->name + "." + std::to_string(alloc_node_counter++);
        alloc.type = op->type;
        alloc.liveness = IntInterval(barrier_stage, barrier_stage);
        alloc.size = 1;
        for (size_t i = 0; i < op->extents.size(); i++) {
            alloc.size *= op->extents[i];
        }
        alloc.size = simplify(alloc.size);
        alloc.memory_type = op->memory_type;

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
        if (device_api == DeviceAPI::OpenGLCompute) {
            return idx;
        }
        Expr base = Variable::make(Int(32), alloc->name + ".offset");
        if (alloc->memory_type == MemoryType::Heap) {
            base += Variable::make(Int(32), heap_name + ".base") / alloc->type.bytes();
        }
        return base + idx;
    }

    Expr visit(const Load *op) override {
        auto it = shared.find(op->name);
        if (it != shared.end()) {
            SharedAllocation *alloc = it->second;
            alloc->liveness.max = barrier_stage;
            Expr predicate = mutate(op->predicate);
            Expr index = mutate_index(alloc, op->index);
            const string &prefix = name_for_memory_type(alloc->memory_type);

            if (device_api == DeviceAPI::OpenGLCompute) {
                return Load::make(op->type, prefix + "_" + alloc->name,
                                  index, op->image, op->param, predicate, op->alignment);
            } else {
                return Load::make(op->type, prefix, index,
                                  op->image, op->param, predicate, ModulusRemainder());
            }

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
            const string &prefix = name_for_memory_type(alloc->memory_type);
            if (device_api == DeviceAPI::OpenGLCompute) {
                return Store::make(prefix + "_" + alloc->name, value, index,
                                   op->param, predicate, op->alignment);
            } else {
                return Store::make(prefix, value, index, op->param, predicate, ModulusRemainder());
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);

        // Set aside the allocations we've found so far.
        vector<SharedAllocation> old;
        old.swap(allocations);

        Stmt body = mutate(op->body);

        // Wrap let expression for any allocations found within
        for (SharedAllocation &s : allocations) {
            if (expr_uses_var(s.size, op->name)) {
                s.size = Let::make(op->name, op->value, s.size);
                s.size = simplify(s.size);
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

                if (!is_const(mem_allocs[free_spaces[i]].max_size_bytes)) {
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
                        mem_allocs.push_back(AllocGroup(allocations[i]));
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

    const string &name_for_memory_type(MemoryType t) {
        if (t == MemoryType::GPUShared) {
            return shared_mem_name;
        } else {
            return heap_name;
        }
    }

public:
    Stmt rewrap_block(Stmt s, const ExtractBlockSize &bs) {

        if (device_api == DeviceAPI::OpenGLCompute) {

            // Individual allocations.
            for (const SharedAllocation &alloc : allocations) {
                const string &prefix = name_for_memory_type(alloc.memory_type);
                s = Allocate::make(prefix + "_" + alloc.name,
                                   alloc.type, alloc.memory_type,
                                   {alloc.size}, const_true(), s);
            }
        } else {
            // One big combined allocation per memory type

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
                     return lhs.max_type_bytes > rhs.max_type_bytes;
                 });

            for (MemoryType memory_type : {MemoryType::GPUShared, MemoryType::Heap}) {

                Expr total_size_bytes = 0;
                int max_type_bytes = 0;
                for (int i = 0; i < (int)(mem_allocs.size()); i++) {
                    if (mem_allocs[i].memory_type == memory_type) {
                        total_size_bytes += mem_allocs[i].max_size_bytes;
                        max_type_bytes = std::max(max_type_bytes, mem_allocs[i].max_type_bytes);
                    }
                }

                if (is_zero(total_size_bytes)) {
                    // No allocations of this type.
                    continue;
                }

                // Align-up the total size in bytes according to the
                // max byte width of all types involved.
                total_size_bytes += max_type_bytes - 1;
                total_size_bytes /= max_type_bytes;
                total_size_bytes *= max_type_bytes;

                // Remove any dependence on the block vars by taking a max
                {
                    Scope<Interval> scope;
                    for (int d = 0; d < bs.blocks_dimensions(); d++) {
                        scope.push(bs.block_var(d).as<Variable>()->name,
                                   Interval(0, bs.num_blocks(d) - 1));
                    }
                    total_size_bytes = simplify(total_size_bytes);
                    Interval in = bounds_of_expr_in_scope(total_size_bytes, scope);
                    internal_assert(in.has_upper_bound())
                        << memory_type
                        << " memory used by GPU kernel varies with the block index in an unbounded way: "
                        << total_size_bytes << "\n";
                    total_size_bytes = in.max;
                }

                const string &prefix = name_for_memory_type(memory_type);
                const string total_size_bytes_name = prefix + ".size";
                Expr total_size_bytes_var = Variable::make(Int(32), total_size_bytes_name);

                if (memory_type == MemoryType::Heap) {
                    // The base offset for shared memory is zero. For
                    // heap memory it's one slice of a global
                    // allocation.
                    Expr block_id = 0;
                    for (int d = bs.blocks_dimensions() - 1; d >= 0; d--) {
                        block_id *= bs.num_blocks(d);
                        block_id += bs.block_var(d);
                    }
                    Expr base = block_id * total_size_bytes_var;
                    s = LetStmt::make(heap_name + ".base", simplify(base), s);
                    heap_bytes_per_block = total_size_bytes;
                } else {
                    s = Allocate::make(prefix, UInt(8), memory_type,
                                       {total_size_bytes_var}, const_true(), s);
                }
                s = LetStmt::make(total_size_bytes_name, total_size_bytes, s);

                // Define an offset for each allocation. The offsets are in
                // elements, not bytes, so that the stores and loads can use
                // them directly.
                for (int i = (int)(mem_allocs.size()) - 1; i >= 0; i--) {
                    if (mem_allocs[i].memory_type != memory_type) {
                        continue;
                    }
                    Expr group_offset = Variable::make(Int(32), "group_" + std::to_string(i) + ".offset");

                    for (const SharedAllocation &alloc : mem_allocs[i].group) {
                        int new_elem_size = alloc.type.bytes();
                        Expr offset = (group_offset / new_elem_size);
                        s = LetStmt::make(alloc.name + ".offset", simplify(offset), s);
                    }

                    // Find the previous allocation of the same memory type
                    int j = i - 1;
                    while (j >= 0) {
                        if (mem_allocs[j].memory_type == memory_type) {
                            break;
                        }
                        j--;
                    }
                    Expr offset = 0;
                    if (j >= 0) {
                        // Build off the last offset
                        offset = Variable::make(Int(32), "group_" + std::to_string(j) + ".offset");
                        int new_elem_size = mem_allocs[i].max_type_bytes;
                        offset += (((mem_allocs[j].max_size_bytes + new_elem_size - 1) / new_elem_size) * new_elem_size);
                    }

                    s = LetStmt::make("group_" + std::to_string(i) + ".offset", simplify(offset), s);
                }
            }
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
        if (!heap_bytes_per_block.defined()) {
            // No heap allocations
            return s;
        }

        Expr total_size = heap_bytes_per_block;
        for (int d = 0; d < bs.blocks_dimensions(); d++) {
            total_size *= bs.num_blocks(d);
        }

        Expr device_interface = make_device_interface_call(device_api);
        string buffer_name = heap_name + ".buffer";
        Expr buffer_var = Variable::make(type_of<halide_buffer_t *>(), buffer_name);

        BufferBuilder builder;
        builder.mins.push_back(0);
        builder.extents.push_back(total_size);
        builder.strides.push_back(1);
        builder.type = UInt(8);
        builder.dimensions = 1;
        Expr buffer = builder.build();

        Expr allocate_heap_call = Call::make(Int(32), "halide_device_malloc",
                                             {buffer_var, device_interface}, Call::Extern);
        string allocate_heap_result_var_name = unique_name('t');
        Expr allocate_heap_result_var = Variable::make(Int(32), allocate_heap_result_var_name);
        Stmt check_allocated =
            AssertStmt::make(allocate_heap_result_var == 0, allocate_heap_result_var);
        Expr device_field = Call::make(Handle(), Call::buffer_get_device, {buffer_var}, Call::Extern);
        s = LetStmt::make(heap_name, device_field, s);
        s = Block::make(check_allocated, s);
        s = LetStmt::make(allocate_heap_result_var_name, allocate_heap_call, s);
        s = Allocate::make(buffer_name, UInt(8),
                           MemoryType::Auto, {}, const_true(), s,
                           buffer, "halide_device_free_as_destructor");

        return s;
    }

    ExtractSharedAndHeapAllocations(DeviceAPI d)
        : in_threads(false),
          barrier_stage(0),
          device_api(d),
          thread_id_var_name(unique_name('t')),
          num_threads_var_name(unique_name('t')),
          heap_name(unique_name("__heap")) {
    }
};

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

            return For::make(op->name, mutate(op->min), mutate(op->extent), op->for_type, op->device_api, body);
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
        for (size_t i = 0; i < op->extents.size(); i++) {
            alloc.size *= op->extents[i];
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
                InjectThreadBarriers i;
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
            body = For::make(thread_id, 0, block_size_x, innermost_loop_type, op->device_api, body);

            // Rewrap the whole thing in other loops over threads
            for (int i = 1; i < block_size.threads_dimensions(); i++) {
                thread_id = "." + thread_names[i];
                body = register_allocs.rewrap(body, thread_id);
                body = For::make("." + thread_names[i], 0, block_size.num_threads(i),
                                 ForType::GPUThread, op->device_api, body);
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
                return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
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
    ZeroGPULoopMins()
        : in_non_glsl_gpu(false) {
    }
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
