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

/** A struct representing the machine parameters to generate the auto-scheduled
 * code for. */
struct MctsParams {
    /** Maximum level of parallelism avalaible. */
    int parallelism;
    /** Size of the last-level cache (in bytes). */
    uint64_t last_level_cache_size;
    /** Indicates how much more expensive is the cost of a load compared to
     * the cost of an arithmetic operation at last level cache. */
    float balance;

    explicit MctsParams(int parallelism, uint64_t llc, float balance)
        : parallelism(parallelism), last_level_cache_size(llc), balance(balance) {
    }

    /** Default machine parameters for generic CPU architecture. */
    static MctsParams generic() {
        std::string params = Internal::get_env_variable("HL_MACHINE_PARAMS");
        if (params.empty()) {
            return MctsParams(16, 16 * 1024 * 1024, 40);
        } else {
            return MctsParams(params);
        }
    }

    /** Convert the MctsParams into canonical string form. */
    std::string to_string() const {
        std::ostringstream o;
        o << parallelism << "," << last_level_cache_size << "," << balance;
        return o.str();
    }

    /** Reconstruct a MctsParams from canonical string form. */
    explicit MctsParams(const std::string &s) {
        std::vector<std::string> v = Internal::split_string(s, ",");
        user_assert(v.size() == 3) << "Unable to parse MctsParams: " << s;
        parallelism = std::atoi(v[0].c_str());
        last_level_cache_size = std::atoll(v[1].c_str());
        balance = std::atof(v[2].c_str());
    }
};

}  // namespace Autoscheduler
}  // namespace Internal

class CostModel {
public:
    virtual ~CostModel() = default;

    // Configure the cost model for the algorithm to be scheduled.
    virtual void set_pipeline_features(const Internal::Autoscheduler::FunctionDAG &dag,
                                       const Internal::Autoscheduler::MctsParams &params) = 0;

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
