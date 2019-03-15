#pragma once

#include <Halide.h>
#include "onnx_converter.h"

struct HalideModel {
  std::shared_ptr<Model> model;
  std::shared_ptr<Halide::Pipeline> rep;
  std::vector<std::string> input_names;
  std::unordered_map<std::string, int> input_types;
  std::vector<std::string> output_names;
  std::vector<int> output_types;
};
