#pragma once

#include <Halide.h>
#include "sysml/halide/onnx_converter.h"
#include "sysml/halide/scheduler/PretrainedCostModel.h"
#include "sysml/halide/scheduler/RandomCostModel.h"

struct HalideMachineParams {
  std::shared_ptr<Halide::MachineParams> mp;
};

struct HalideTarget {
  std::shared_ptr<Halide::Target> target;
};

struct HalideCostModel {
  std::shared_ptr<Halide::CostModel> cost_model;
};

struct HalideModel {
  std::shared_ptr<Model> model;
  std::shared_ptr<Halide::Pipeline> rep;
  std::vector<std::string> input_names;
  std::unordered_map<std::string, int> input_types;
  std::vector<std::string> output_names;
  std::vector<int> output_types;
};
