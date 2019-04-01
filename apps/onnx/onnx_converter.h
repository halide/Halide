#ifndef CONVERT_MODEL_H_
#define CONVERT_MODEL_H_

#include <unordered_map>
#include <vector>
#include "Halide.h"
#include "onnx/onnx_pb.h"

struct Tensor {
  std::string name;
  onnx::TensorProto::DataType type;
  std::vector<Halide::Expr> shape;
  Halide::Func rep;
};

struct Node {
  std::vector<Tensor> inputs;
  std::vector<Tensor> outputs;

  std::vector<Halide::Expr> requirements;
};

Node convert_node(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs,
    const std::string& device);

struct Model {
  std::unordered_map<std::string, Halide::ImageParam> inputs;
  std::unordered_map<std::string, Tensor> outputs;

  std::unordered_map<std::string, Tensor> tensors;

  std::vector<Halide::Expr> requirements;
};

Model convert_model(const onnx::ModelProto& model, const std::string& device);

#endif
