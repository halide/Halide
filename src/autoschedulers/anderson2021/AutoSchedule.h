#ifndef AUTO_SCHEDULE_H
#define AUTO_SCHEDULE_H

#include <random>
#include <vector>

#include "CostModel.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "PerfectHashMap.h"
#include "SearchSpace.h"
#include "State.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

void find_and_apply_schedule(FunctionDAG &dag,
                             const std::vector<Function> &outputs,
                             const Anderson2021Params &params,
                             const Target &target,
                             CostModel *cost_model,
                             int beam_size,
                             StageMapOfScheduleFeatures *schedule_features);

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // AUTO_SCHEDULE_H
