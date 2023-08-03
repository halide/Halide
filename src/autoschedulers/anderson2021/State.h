#ifndef STATE_H
#define STATE_H

#include "ASLog.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "LoopNest.h"
#include "PerfectHashMap.h"
#include <set>
#include <unordered_set>
#include <vector>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::unordered_set;
using std::vector;

bool verify_memoized_features();

bool is_memoize_blocks_enabled();

double get_stack_memory_adjustment_factor();

constexpr int kLocalMemoryLimit = 524288;  // 512 KB

// Stack memory limit = Total GPU Memory / (# of SMs * maximum threads per SM)
//                    = 103232 bytes
// Not all 103232 bytes will be free for allocations so reduce it by factor to
// allow a buffer
int64_t get_stack_memory_limit();

bool use_adjusted_tilings();

bool compute_root_and_inline_only();

struct NoOpMutator {
    void operator()(LoopNest *new_loop_nest) const {
    }
};

template<typename PostCreateMutator>
void deep_copy_loop_nest(LoopNest *new_loop_nest,
                         const LoopNest *new_loop_nest_parent,
                         const IntrusivePtr<const LoopNest> &existing_loop_nest,
                         const PostCreateMutator &post_create_mutator) {
    new_loop_nest->copy_from(*existing_loop_nest);

    for (std::size_t i = 0, N = new_loop_nest->children.size(); i < N; ++i) {
        LoopNest *new_child = new LoopNest;
        new_loop_nest->children[i] = new_child;
        deep_copy_loop_nest(new_child, new_loop_nest, existing_loop_nest->children[i], post_create_mutator);
    }

    post_create_mutator(new_loop_nest);
}

using LoopNestMap = map<const LoopNest *, pair<const LoopNest *, int>>;

template<typename PostCreateMutator>
LoopNest *deep_copy_loop_nest(const IntrusivePtr<const LoopNest> &loop_nest,
                              const PostCreateMutator &post_create_mutator) {
    LoopNest *new_loop_nest = new LoopNest;
    deep_copy_loop_nest(new_loop_nest, nullptr, loop_nest, post_create_mutator);
    return new_loop_nest;
}

struct State {
    mutable RefCount ref_count;
    IntrusivePtr<const LoopNest> root;
    IntrusivePtr<const State> parent;
    double cost = 0;
    std::vector<double> cost_per_stage;
    NodeMap<bool> always_consider_inline;
    int num_decisions_made = 0;
    bool penalized = false;
    string schedule_source;

    State() = default;
    State(const State &) = delete;
    State(State &&) = delete;
    void operator=(const State &) = delete;
    void operator=(State &&) = delete;

    uint64_t structural_hash(int depth) const;

    // Compute the parent and depth of every loop nest node
    void compute_loop_nest_parents(LoopNestMap &p,
                                   const LoopNest *here,
                                   int depth) const;

    const LoopNest *deepest_common_ancestor(const LoopNestMap &parent,
                                            const LoopNest *a,
                                            const LoopNest *b) const;

    // We use the post_create_mutator so that the loop nests can be modified
    // before they become IntrusivePtr<const LoopNest> as children and cannot be modified
    template<typename PostCreateMutator>
    LoopNest *create_feature_root(const PostCreateMutator &post_create_mutator) const {
        LoopNest *new_root = new LoopNest;
        deep_copy_loop_nest<PostCreateMutator>(new_root, nullptr, root, post_create_mutator);
        return new_root;
    }

    bool has_loop_nest_without_thread_loops() const;

    bool has_compute_root_loops_without_blocks() const;

    struct FeatureLoopNestMutator {
        const Anderson2021Params &params;
        const Target &target;

        void operator()(LoopNest *new_loop_nest) const;

        // In phase 2, any compute_root loop marked 'none' will be split into
        // blocks, threads, and serial loops. To enable the cost model to make a
        // meaningful prediction on these pre-split loops, we assume a split into
        // blocks and threads with a single full warp (if possible)
        void split_compute_root_loops(LoopNest *loop_nest) const;

        // If a loop nest does not have thread loops, split the outermost serial
        // loops to create thread loops with extents 1
        void add_outer_thread_loops(LoopNest *loop_nest) const;
    };

    IntrusivePtr<const LoopNest> get_root_for_features(const Anderson2021Params &params,
                                                       const Target &target) const;

    void set_gpu_store_site(const LoopNestMap &parent,
                            const LoopNest *loop,
                            LoopNest::Sites &site) const;

    bool compute_featurization(const FunctionDAG &dag,
                               const Anderson2021Params &params,
                               const Target &target,
                               StageMap<ScheduleFeatures> *features,
                               Statistics &stats,
                               bool verbose = false) const;

    void save_featurization(const FunctionDAG &dag,
                            const Anderson2021Params &params,
                            const Target &target,
                            std::ostream &out) const;

    bool contains_store_at(const set<const FunctionDAG::Node *> &outermost_store_at,
                           const IntrusivePtr<const LoopNest> &parent) const;

    // For GPU, only allow store_at root or inside the outermost loop nest. Any
    // store_ats further in will be hoisted and expanded, increasing the
    // amount of shared memory required.
    bool contains_store_at_further_in_than_outermost() const;

    bool has_dynamic_allocation_inside_thread() const;

    bool exceeds_serial_extents_limit(const Target &target) const;

    int64_t get_shared_mem_alloc_size(const LoopNest *block,
                                      const LoopNest *loop) const;

    bool exceeds_shared_memory_limit(const Anderson2021Params &params,
                                     const Target &target) const;

    bool exceeds_local_memory_limit(const Anderson2021Params &params,
                                    const Target &target) const;

    bool calculate_cost(const FunctionDAG &dag,
                        const Anderson2021Params &params,
                        const Target &target,
                        CostModel *cost_model,
                        Statistics &stats,
                        bool verbose = false);

    // Make a child copy of this state. The loop nest is const (we
    // make mutated copies of it, rather than mutating it), so we can
    // continue to point to the same one and so this is a cheap
    // operation.
    IntrusivePtr<State> make_child() const;

    void dump() const;

    void print_compute_locations() const;

    void fuse_gpu_blocks(LoopNest::StageScheduleState *state,
                         Stage &stage,
                         const vector<VarOrRVar> &parallel_vars,
                         const vector<int64_t> &parallel_extents,
                         const vector<int> &constant_extents) const;

    void mark_gpu_blocks(LoopNest::StageScheduleState *state,
                         Stage &stage,
                         const vector<VarOrRVar> &parallel_vars,
                         const vector<int64_t> &parallel_extents) const;

    bool mark_gpu_threads(LoopNest::StageScheduleState *state,
                          Stage &stage,
                          std::unordered_set<std::string> &new_serial_vars,
                          std::ostringstream &staged_funcs_schedule_source) const;

    bool can_fuse_gpu(const vector<int64_t> &parallel_extents) const;

    // Apply the schedule represented by this state to a Halide
    // Pipeline. Also generate source code for the schedule for the
    // user to copy-paste to freeze this schedule as permanent artifact.
    void apply_schedule(const FunctionDAG &dag,
                        const Anderson2021Params &params,
                        const Target &target);

    bool should_always_consider_inline(const FunctionDAG::Node *node) const;
    void add_to_always_consider_inline_options(const FunctionDAG::Node *node);
    void update_always_consider_inline_options(const FunctionDAG::Node *node);

    const LoopNest *deepest_valid_compute_location(const Anderson2021Params &params,
                                                   const LoopNestMap &parent,
                                                   const FunctionDAG::Node &node,
                                                   const LoopNest *loop,
                                                   const LoopNest *root,
                                                   StageMap<int64_t> &total_shared_mem_alloc_sizes) const;
    int64_t total_loop_extents_of_ancestors(const LoopNestMap &parent,
                                            const LoopNest *loop) const;
};

// A priority queue of states, sorted according to increasing
// cost. Never shrinks, to avoid reallocations.
// Can't use std::priority_queue because it doesn't support unique_ptr.
class StateQueue {
private:
    struct CompareStates {
        bool operator()(const IntrusivePtr<State> &a, const IntrusivePtr<State> &b) const {
            return a->cost > b->cost;
        }
    };

    std::vector<IntrusivePtr<State>> storage;
    size_t sz = 0;

public:
    void emplace(IntrusivePtr<State> &&s) {
        if (sz >= storage.size()) {
            storage.resize(std::max(sz * 2, (size_t)64));
        }
        internal_assert(sz < storage.size()) << sz << " " << storage.size() << "\n";
        storage[sz] = std::move(s);
        sz++;
        std::push_heap(storage.begin(), storage.begin() + sz, CompareStates{});
    }

    IntrusivePtr<State> pop() {
        internal_assert(sz <= storage.size()) << sz << " " << storage.size() << "\n";
        std::pop_heap(storage.begin(), storage.begin() + sz, CompareStates{});
        sz--;
        return std::move(storage[sz]);
    }

    const IntrusivePtr<State> &top() {
        return storage[0];
    }

    bool empty() const {
        return sz == 0;
    }

    size_t size() const {
        return sz;
    }

    void swap(StateQueue &other) {
        storage.swap(other.storage);
        std::swap(sz, other.sz);
    }

    IntrusivePtr<State> operator[](int idx) const {
        return storage[idx];
    }

    void resort() {
        std::make_heap(storage.begin(), storage.begin() + sz, CompareStates{});
    }

    void clear() {
        for (size_t i = 0; i < sz; i++) {
            storage[i] = IntrusivePtr<State>{};
        }
        sz = 0;
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // STATE_H
