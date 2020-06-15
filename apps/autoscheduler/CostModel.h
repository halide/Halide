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
}  // namespace Autoscheduler
}  // namespace Internal

class CostModel {
public:
    virtual ~CostModel() = default;

    // Configure the cost model for the algorithm to be scheduled.
    virtual void set_pipeline_features(const Internal::Autoscheduler::FunctionDAG &dag,
                                       const MachineParams &params) = 0;

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
