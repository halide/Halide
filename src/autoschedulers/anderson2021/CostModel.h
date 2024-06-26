#ifndef COST_MODEL_H
#define COST_MODEL_H

#include <string>

#include "FunctionDAG.h"
#include "HalideBuffer.h"
#include "PerfectHashMap.h"

// An abstract base class for a cost model.
namespace Halide {

namespace Internal {
namespace Autoscheduler {

typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

struct Anderson2021Params {
    /** Maximum level of parallelism available. */
    int parallelism = 16;

    /** Beam size to use in the beam search. Defaults to 32. Use 1 to get a greedy search instead.
     * Formerly HL_BEAM_SIZE */
    int beam_size = 32;

    /** percent chance of accepting each state in the beam.
     * Normalized by the number of decisions made, so 5 would be there's a 5 percent chance of never rejecting any states.
     * Formerly HL_RANDOM_DROPOUT */
    int random_dropout = 100;

    /** Random seed used by the random dropout. If 0, use time().
     * Formerly HL_SEED */
    int64_t random_dropout_seed = 0;

    /** When training or schedule, read weights from this directory or file.
     * (If path ends in `.weights` it is written as a single file, otherwise a directory of files.)
     * Formerly HL_WEIGHTS_DIR */
    std::string weights_path;

    /** If set to nonzero value: limits the search space to that of Mullapudi et al.
     * Formerly HL_NO_SUBTILING */
    int disable_subtiling = 0;

    /** If set to nonzero value, only a random subset of the generated tilings for each stage will be accepted into the beam.
     * Formerly HL_RANDOMIZE_TILINGS */
    int randomize_tilings = 0;

    /** Expects a string of four 0/1 values that allow/disallow the following options:
     * compute root, inline, compute at the block level, compute at the thread level
     * e.g. 1000 would allow compute root only
     * Formerly HL_SEARCH_SPACE_OPTIONS */
    std::string search_space_options = "1111";

    /** If set to nonzero value, run a pre-pass where only compute_root and inline scheduling options are considered.
     * Formerly HL_FREEZE_INLINE_COMPUTE_ROOT */
    int freeze_inline_compute_root = 0;

    /** If nonempty, load the initial (partial) schedule from the given file.
     * Formerly PARTIAL_SCHEDULE */
    std::string partial_schedule_path;

    /** User-requested specific number of passes. Ignored if 0.
     * Formerly HL_NUM_PASSES */
    int num_passes = 0;

    /** TODO: document me
     * Formerly HL_STACK_FACTOR */
    double stack_factor = 0.95f;

    /** TODO: document me
     * Formerly HL_SHARED_MEMORY_LIMIT */
    int shared_memory_limit_kb = 48;

    /** TODO: document me
     * Formerly HL_SHARED_MEMORY_SM_LIMIT */
    int shared_memory_sm_limit_kb = 96;

    /** TODO: document me
     * Formerly HL_ACTIVE_BLOCK_LIMIT */
    int active_block_limit = 32;

    /** TODO: document me
     * Formerly HL_ACTIVE_WARP_LIMIT */
    int active_warp_limit = 64;
};

}  // namespace Autoscheduler
}  // namespace Internal

class CostModel {
public:
    virtual ~CostModel() = default;

    // Configure the cost model for the algorithm to be scheduled.
    virtual void set_pipeline_features(const Internal::Autoscheduler::FunctionDAG &dag,
                                       const Internal::Autoscheduler::Anderson2021Params &params) = 0;

    // Enqueue a schedule to be evaluated. Will annotate the value located at cost_ptr when the evaluation takes place.
    // Note that the dag argument should correspond to the dag specified previously when calling set_pipeline_features.
    virtual void enqueue(const Internal::Autoscheduler::FunctionDAG &dag,
                         const Halide::Internal::Autoscheduler::StageMapOfScheduleFeatures &schedule_feats,
                         double *cost_ptr,
                         std::vector<double> *cost_per_stage_ptr) = 0;

    // Evaluate all schedules in the queue.
    virtual void evaluate_costs() = 0;

    // Discard all schedules in the queue.
    virtual void reset() = 0;
};

}  // namespace Halide

#endif  // COST_MODEL_H
