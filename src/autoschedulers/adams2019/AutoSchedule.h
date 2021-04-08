#include "CostModel.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "PerfectHashMap.h"
#include <vector>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

void find_and_apply_schedule(FunctionDAG &dag, const std::vector<Function> &outputs, const MachineParams &params,
                             CostModel *cost_model, int beam_size, StageMapOfScheduleFeatures *schedule_features);

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
