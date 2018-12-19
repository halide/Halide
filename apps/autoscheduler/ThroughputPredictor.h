#include "HalideBuffer.h"

namespace Halide {
class ThroughputPredictor {
 public:
  virtual ~ThroughputPredictor() = default;
  virtual void set_pipeline_features(const Runtime::Buffer<float> &pipeline_feats, int n) = 0;
  virtual void enqueue(int ns, Runtime::Buffer<float> *schedule_feats, double *cost_ptr) = 0;
  virtual void evaluate_costs() = 0;
  virtual void reset() = 0;
};

}
