#ifndef HL_MCTS_CPU_STATE_H
#define HL_MCTS_CPU_STATE_H

// TODO(rootjalex): fix includes.

#include "CostModel.h"
#include "Halide.h"
#include "LoopNest.h"
#include <limits>       // std::numeric_limits
#include <string>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

// TODO(rootjalex): Might be able to add to these.
enum class CPU_ScheduleAction {
    Error, // for error checking later.
    Inline,
    Vectorize,
    Tile,
    ComputeRoot,
    Input,
    Parallelize,
    Empty,          // Used for first TreeNode *only*.
};

// Possible actions to be taken from an exploration State
struct CPU_Action {
    // Whether or not this action has been explored yet.
    bool explored = false;

    CPU_Action() = delete;
    CPU_Action(const CPU_Action &_action)= default;
    CPU_Action(CPU_ScheduleAction _action, IntrusivePtr<const LoopNest> _root) :
        schedule_action(_action), root(_root) {}

    // TODO(rootjalex): figure out what else is needed.
    // Action to take.
    CPU_ScheduleAction schedule_action;
    // Root is different for some schedules.
    IntrusivePtr<const LoopNest> root;

    // Needed for smart pruning, Inlining, etc.
    FunctionDAG::Node *node_ptr;

    static CPU_Action Default() {
        return CPU_Action(CPU_ScheduleAction::Empty, nullptr);
    }
};

class CPU_State {
    // Root LoopNest for this state.
    IntrusivePtr<const LoopNest> root;

    // TODO(rootjalex): do we need a parent pointer?
    // IntrusivePtr<const State> parent;

    // This is used for error checking.
    // TODO(rootjalex): remove this once reasonably
    //                  sure that everything works.
    size_t n_decisions_made;

    // Minimum cost found by exploring this state.
    double minimum_cost = std::numeric_limits<double>::max();

    // Required information to be able to generate possible actions.
    // All State for a run of MCTS should have the same pointers.
    // TODO(rootjalex): should these be static members then?
    const FunctionDAG *dag_ptr;
    const MachineParams *params_ptr;
    CostModel *model_ptr;
    int64_t memory_limit = 0;

    // Whether or not this state was already checked for pruning.
    // For now, only Inline states are prepruned - that might change.
    bool prepruned = false;

    // Saving the features for calculating cost.
    mutable StageMap<ScheduleFeatures> features;

public:
    CPU_State() = delete;
    CPU_State(const CPU_State &_state) = default;
    CPU_State(CPU_State &&_state) = default;
    CPU_State &operator=(const CPU_State &_state) = default;
    CPU_State(const FunctionDAG *_dag_ptr, const MachineParams *_params_ptr,
              CostModel *_model_ptr, IntrusivePtr<const LoopNest> _root, int n_decisions, int64_t _memory_limit = 0) :
        root(_root), n_decisions_made(n_decisions), dag_ptr(_dag_ptr),
        params_ptr(_params_ptr), model_ptr(_model_ptr), memory_limit(_memory_limit) {
            // internal_assert(dag_ptr) << "CPU_State received nullptr dag_ptr\n";
            // internal_assert(params_ptr) << "CPU_State received nullptr params_ptr\n";
            // internal_assert(model_ptr) << "CPU_State received nullptr model_ptr\n";
        }

    // This is likely very expensive, but generate all possible
    // actions that we can take from this state.
    std::vector<CPU_Action> generate_possible_actions() const;

private:
    std::vector<CPU_Action> generate_injected_realizations(const FunctionDAG::Node *node) const;
    std::vector<CPU_Action> generate_parallel_realizations(const FunctionDAG::Node *node) const;

public:
    // Produce the State made by performing this action.
    CPU_State take_action(const CPU_Action &action) const;

    // Get the value stored by this state.
    // Could be minimum or acerage of children traversed.
    double get_value() const;

    // TODO(rootjalex): understand what this should do.
    bool is_terminal() const;

    // Checks if this state should be pruned or not.
    bool is_valid() const;

    // This is probaby a call to calculate_cost.
    // TODO(rootjalex): what do we need to store
    //                  in order to make this call?
    double calculate_cost() const;

    // Update the value of this state. Passed by
    // reference in case it needs to be updated.
    bool update(double &cost_value);

    // Return a stored or calculated exploration value.
    // This might be the minimum cost found so far, or
    // the average cost of child nodes that have been explored.
    double get_exploitation_value(uint32_t num_visits);

    // Apply this State to the FunctionDag.
    std::string apply_schedule();

    void dump() const;
};

// This is used to early-out for certain prunable States.
// Returns true if this LoopNest should not be a valid State.
bool prunable(const FunctionDAG *dag_ptr, const MachineParams *params_ptr, const LoopNest *root_ptr, StageMap<ScheduleFeatures> &features, int64_t memory_limit);

// Used by the above to check if a LoopNest is prunable.
void compute_featurization(const FunctionDAG *dag_ptr, const MachineParams *params_ptr, const LoopNest *root_ptr, StageMap<ScheduleFeatures> *features);

// Compute the parent and depth of every loop nest node
void compute_loop_nest_parents(map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                const LoopNest *here, int depth);

// Find the deepest common ancestor of `a` and `b`.
const LoopNest *deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                        const LoopNest *a, const LoopNest *b);

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // HL_MCTS_CPU_STATE_H
