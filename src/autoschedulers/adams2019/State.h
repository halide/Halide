#ifndef STATE_H
#define STATE_H

#include "ASLog.h"
#include "Caching.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Featurization.h"
#include "Halide.h"
#include "LoopNest.h"
#include "PerfectHashMap.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct State {
    mutable RefCount ref_count;
    IntrusivePtr<const LoopNest> root;
    IntrusivePtr<const State> parent;
    double cost = 0;
    int num_decisions_made = 0;
    bool penalized = false;
    string schedule_source;

    static int cost_calculations;

    State() = default;
    State(const State &) = delete;
    State(State &&) = delete;
    void operator=(const State &) = delete;
    void operator=(State &&) = delete;

    uint64_t structural_hash(int depth) const;

    // Compute the parent and depth of every loop nest node
    void compute_loop_nest_parents(map<const LoopNest *, pair<const LoopNest *, int>> &p,
                                   const LoopNest *here, int depth) const;

    const LoopNest *deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                            const LoopNest *a, const LoopNest *b) const;

    void compute_featurization(const FunctionDAG &dag,
                               const MachineParams &params,
                               StageMap<ScheduleFeatures> *features,
                               const CachingOptions &cache_options);

    void save_featurization(const FunctionDAG &dag,
                            const MachineParams &params,
                            const CachingOptions &cache_options,
                            std::ostream &out);

    bool calculate_cost(const FunctionDAG &dag, const MachineParams &params,
                        CostModel *cost_model, const CachingOptions &cache_options,
                        int64_t memory_limit, bool verbose = false);

    // Make a child copy of this state. The loop nest is const (we
    // make mutated copies of it, rather than mutating it), so we can
    // continue to point to the same one and so this is a cheap
    // operation.
    IntrusivePtr<State> make_child() const;

    // Generate the successor states to this state
    void generate_children(const FunctionDAG &dag,
                           const MachineParams &params,
                           CostModel *cost_model,
                           int64_t memory_limit,
                           std::function<void(IntrusivePtr<State> &&)> &accept_child,
                           Cache *cache) const;

    void dump() const;

    // Apply the schedule represented by this state to a Halide
    // Pipeline. Also generate source code for the schedule for the
    // user to copy-paste to freeze this schedule as permanent artifact.
    void apply_schedule(const FunctionDAG &dag, const MachineParams &params);
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // STATE_H
