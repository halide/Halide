#ifndef COST_MODEL_H
#define COST_MODEL_H

#include <string>

#include "HalideBuffer.h"

// An abstract base class for a cost model.
namespace Halide {

class CostModel {
public:
    virtual ~CostModel() = default;

    // Configure the cost model for the algorithm to be scheduled.
    virtual void set_pipeline_features(const Halide::Runtime::Buffer<float> &pipeline_feats, int n) = 0;

    // Enqueue a schedule to be evaluated. Returns a buffer of
    // schedule_features that should be filled in by the caller.
    virtual void enqueue(int ns, Halide::Runtime::Buffer<float> *schedule_feats, double *cost_ptr) = 0;

    // Evaluate all schedules in the queue.
    virtual void evaluate_costs() = 0;

    // Discard all schedules in the queue.
    virtual void reset() = 0;

    // Update model weights using true measured runtimes.
    virtual float backprop(const Halide::Runtime::Buffer<const float> &true_runtimes, float learning_rate) = 0;

    // Save the model weights to disk.
    virtual void save_weights() = 0;
};

}  // namespace Halide

#endif  // COST_MODEL_H
