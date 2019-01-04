#include "Halide.h"
#include "CostModel.h"
#include "FunctionDAG.h"
#include "PerfectHashMap.h"

namespace  Halide {
namespace Internal {

typedef PerfectHashMap<FunctionDAG::Node::Stage, ScheduleFeatures> StageMapOfScheduleFeatures;

extern "C" {
void find_and_apply_schedule(FunctionDAG& dag, const vector<Function> &outputs, const MachineParams &params,
			     CostModel* cost_model, int beam_size, StageMapOfScheduleFeatures* schedule_features);
}

}
}
