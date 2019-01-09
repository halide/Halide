#include "Halide.h"
#include "CostModel.h"
#include "FunctionDAG.h"
#include "PerfectHashMap.h"
#include "Featurization.h"
#include <vector>

namespace Halide {
namespace Internal {

typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

void find_and_apply_schedule(FunctionDAG& dag, const std::vector<Function> &outputs, const MachineParams &params,
			     CostModel* cost_model, int beam_size, StageMapOfScheduleFeatures* schedule_features);

}
}
