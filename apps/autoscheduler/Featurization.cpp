#include "Featurization.h"
#include "FunctionDAG.h"

namespace Halide {
void compute_pipeline_featurization(const Pipeline &pipeline, const Target& tgt, const MachineParams &params, std::unordered_map<Stage, PipelineFeatures, StageHasher> *features) {
  std::vector<Internal::Function> outputs;
  for (Func f : pipeline.outputs()) {
    outputs.push_back(f.function());
  }
  Internal::FunctionDAG dag(outputs, params, tgt);

  // Annotate the DAG with pipeline features
  dag.featurize();

  // Extract the pipeline features
  for (const Internal::FunctionDAG::Node &node : dag.nodes) {
    for (const Internal::FunctionDAG::Node::Stage& stage : node.stages) {
      features->emplace(stage.stage, stage.features);
    }
  }
}
}
