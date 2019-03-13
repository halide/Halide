#pragma once

#include <unordered_map>
#include <vector>
#include "Halide.h"
#include "onnx.pb.h"

struct Tensor {
  std::string name;
  onnx::TensorProto::DataType type;
  std::vector<Halide::Expr> shape;
  Halide::Func rep;
};

struct Node {
  std::vector<Tensor> inputs;
  std::vector<Tensor> outputs;

  std::vector<Halide::Func> internal_funcs;
};

Node ConvertNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs,
    const std::string& device);

struct Model {
  std::unordered_map<std::string, Halide::ImageParam> inputs;
  std::unordered_map<std::string, Tensor> outputs;

  std::unordered_map<std::string, Tensor> tensors;
};

Model ConvertModel(const onnx::ModelProto& model, const std::string& device);
