#ifndef COST_MODEL_H
#define COST_MODEL_H

#include "HalideBuffer.h"

class CostModel {
public:
    virtual ~CostModel() = default;
    virtual void set_pipeline_features(const Halide::Runtime::Buffer<float> &pipeline_feats, int n) = 0;
    virtual void enqueue(int ns, Halide::Runtime::Buffer<float> *schedule_feats, double *cost_ptr) = 0;
    virtual void evaluate_costs() = 0;
    virtual void reset() = 0;
    virtual float backprop(const Halide::Runtime::Buffer<const float> &true_runtimes, float learning_rate) = 0;
    virtual void save_weights() = 0;
    static std::unique_ptr<CostModel> make_default();
};

#endif
