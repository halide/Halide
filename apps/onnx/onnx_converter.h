#ifndef CONVERT_MODEL_H_
#define CONVERT_MODEL_H_

#include "Halide.h"
#include "onnx/onnx.pb.h"
#include <unordered_map>
#include <vector>

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
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs);

struct Model {
    std::unordered_map<std::string, Halide::ImageParam> inputs;
    std::unordered_map<std::string, Tensor> outputs;

    std::unordered_map<std::string, Tensor> tensors;

    std::vector<Halide::Expr> requirements;
};

// Layout of the inputs and outputs to the model.
enum IOLayout {
    Native = 0,
    NumPy = 1,
};
Model convert_model(const onnx::ModelProto &model, const std::unordered_map<std::string, int> &expected_dim_sizes, IOLayout layout);

Halide::Type get_halide_type(const Tensor &tensor);

void compute_output_shapes(
    const Model &model,
    const std::map<std::string, std::vector<int>> &input_shapes,
    std::map<std::string, std::vector<int>> *output_shapes);

void extract_expected_input_shapes(
    const Model &model,
    std::map<std::string, std::vector<int>> *input_shapes);

void compute_expected_output_shapes(
    const Model &model,
    std::map<std::string, std::vector<int>> *output_shapes);

#endif
