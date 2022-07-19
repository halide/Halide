#ifndef COST_MODEL_H
#define COST_MODEL_H

#include <string>

#include "Featurization.h"
#include "FunctionDAG.h"
#include "HalideBuffer.h"
#include "PerfectHashMap.h"

// An abstract base class for a cost model.
namespace Halide {

namespace Internal {
namespace Autoscheduler {

typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

struct Adams2019Params {
    /** Maximum level of parallelism available. */
    int parallelism = 16;

#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
    // nothing
#else
    /** Beam size to use in the beam search. Defaults to 32. Use 1 to get a greedy search instead.
     * Formerly HL_BEAM_SIZE */
    int beam_size = 32;

    /** percent chance of accepting each state in the beam.
     * Normalized by the number of decisions made, so 5 would be there's a 5 percent chance of never rejecting any states.
     * Formerly HL_RANDOM_DROPOUT */
    int random_dropout = 100;

    /** Random seed used by the random dropout. If 0, use time().
     * Formerly HL_SEED */
    int random_dropout_seed = 0;
#endif
};

}  // namespace Autoscheduler
}  // namespace Internal

class CostModel {
public:
    virtual ~CostModel() = default;

    // Configure the cost model for the algorithm to be scheduled.
    virtual void set_pipeline_features(const Internal::Autoscheduler::FunctionDAG &dag,
                                       const Internal::Autoscheduler::Adams2019Params &params) = 0;

    // Enqueue a schedule to be evaluated. Will annotate the value located at cost_ptr when the evaluation takes place.
    // Note that the dag argument should correspond to the dag specified previously when calling set_pipeline_features.
    virtual void enqueue(const Internal::Autoscheduler::FunctionDAG &dag,
                         const Halide::Internal::Autoscheduler::StageMapOfScheduleFeatures &schedule_feats,
                         double *cost_ptr) = 0;

    // Evaluate all schedules in the queue.
    virtual void evaluate_costs() = 0;

    // Discard all schedules in the queue.
    virtual void reset() = 0;
};

}  // namespace Halide

#endif  // COST_MODEL_H
