#ifndef STATE_H
#define STATE_H

#include "ASLog.h"
#include "Cache.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Featurization.h"
#include "Halide.h"
#include "LoopNest.h"
#include "PerfectHashMap.h"
#include <map>
#include <utility>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

// A struct representing an intermediate state in the tree search.
// It represents a partial schedule for some pipeline.
struct State {
    mutable RefCount ref_count;
    // The LoopNest this state corresponds to.
    IntrusivePtr<const LoopNest> root;
    // The parent that generated this state.
    IntrusivePtr<const State> parent;
    // Cost of this state, as evaluated by the cost model.
    double cost = 0;
    // Number of decisions made at this state (used for finding which DAG node to schedule).
    int num_decisions_made = 0;
    // Penalization is determined based on structural hash during beam search.
    bool penalized = false;

    // The C++ source code of the generated schedule for this State.
    // Computed if `apply_schedule` is called.
    string schedule_source;

    // The number of times a cost is enqueued into the cost model,
    // for all states.
    static int cost_calculations;

    State() = default;
    State(const State &) = delete;
    State(State &&) = delete;
    void operator=(const State &) = delete;
    void operator=(State &&) = delete;

    // Compute a structural hash based on depth and num_decisions_made.
    // Defers to root->structural_hash().
    uint64_t structural_hash(int depth) const;

    // Compute the featurization of this state (based on `root`),
    // and store features in `features`. Defers to `root->compute_features()`.
    void compute_featurization(const FunctionDAG &dag,
                               const Adams2019Params &params,
                               StageMap<ScheduleFeatures> *features,
                               const CachingOptions &cache_options);

    // Calls `compute_featurization` and prints those features to `out`.
    void save_featurization(const FunctionDAG &dag,
                            const Adams2019Params &params,
                            const CachingOptions &cache_options,
                            std::ostream &out);

    // Performs some pruning to decide if this state is worth queuing in
    // the cost_model. If it is, calls `cost_model->enqueue` and returns true,
    // otherwise sets `cost` equal to a large value and returns false.
    bool calculate_cost(const FunctionDAG &dag, const Adams2019Params &params,
                        CostModel *cost_model, const CachingOptions &cache_options,
                        int64_t memory_limit, int verbosity = 99);

    // Make a child copy of this state. The loop nest is const (we
    // make mutated copies of it, rather than mutating it), so we can
    // continue to point to the same one and so this is a cheap
    // operation.
    IntrusivePtr<State> make_child() const;

    // Generate the successor states to this state.
    // If they are not pruned by `calculate_cost()`,
    // then calls `accept_child()` on them.
    void generate_children(const FunctionDAG &dag,
                           const Adams2019Params &params,
                           CostModel *cost_model,
                           int64_t memory_limit,
                           std::function<void(IntrusivePtr<State> &&)> &accept_child,
                           Cache *cache) const;

    // Dumps cost, the `root` LoopNest, and then `schedule_source` to `os`.
    void dump(std::ostream &os) const;

    // Apply the schedule represented by this state to a Halide
    // Pipeline. Also generate source code for the schedule for the
    // user to copy-paste to freeze this schedule as permanent artifact.
    // Also fills `schedule_source`.
    void apply_schedule(const FunctionDAG &dag, const Adams2019Params &params);
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // STATE_H
