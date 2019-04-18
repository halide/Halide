#include "Halide.h"
#include "onnx_converter.h"
#include <fstream>
#include <map>

namespace {

// TODO: This should be an onnx_converter API.
Halide::Type get_halide_type(const Tensor &tensor) {
    switch (tensor.type) {
    case onnx::TensorProto_DataType_FLOAT:
        return Halide::Float(32);
    case onnx::TensorProto_DataType_DOUBLE:
        return Halide::Float(64);
    case onnx::TensorProto_DataType_INT8:
        return Halide::Int(8);
    case onnx::TensorProto_DataType_INT16:
        return Halide::Int(16);
    case onnx::TensorProto_DataType_INT32:
        return Halide::Int(32);
    case onnx::TensorProto_DataType_UINT8:
        return Halide::UInt(8);
    case onnx::TensorProto_DataType_UINT16:
        return Halide::UInt(16);
    case onnx::TensorProto_DataType_UINT32:
        return Halide::UInt(32);
    case onnx::TensorProto_DataType_INT64:
        return Halide::Int(64);
    case onnx::TensorProto_DataType_BOOL:
        return Halide::Bool();
    default:
        throw std::domain_error("Unsupported or unknown target type");
    }
    throw std::domain_error("Unsupported or unknown target type");
}

class OnnxModelConverterGenerator
    : public Halide::Generator<OnnxModelConverterGenerator> {
public:
    GeneratorParam<std::string> model_file_path{ "model_file_path", "" };

    void configure() {
        onnx::ModelProto onnx_model;
        std::fstream input(model_file_path.value(), std::ios::in | std::ios::binary);
        if (!input) {
            throw std::invalid_argument(
                "Can't read model file" + model_file_path.value());
        }
        std::stringstream buffer;
        buffer << input.rdbuf();
        if (!onnx_model.ParseFromString(buffer.str())) {
            throw std::invalid_argument(
                "Can't parse model file" + model_file_path.value());
        }

        converted_model_ = convert_model(onnx_model);
        for (const auto &input : converted_model_.inputs) {
            model_inputs_[input.first] = add_input<Buffer<>>(
                input.first,
                input.second.type(),
                input.second.parameter().dimensions());
        }
        for (const auto &output : converted_model_.outputs) {
            model_outputs_[output.first] = add_output<Buffer<>>(
                output.first,
                get_halide_type(output.second),
                output.second.rep.dimensions());
        }
    }

    void generate() {
        for (auto const &input : model_inputs_) {
            auto tensor = converted_model_.tensors.find(input.first);
            if (tensor == converted_model_.tensors.end()) {
                throw std::domain_error("Can't bind input " + input.first);
            }
            tensor->second.rep = *input.second;
        }
        for (auto &output : model_outputs_) {
            auto model_output = converted_model_.outputs.find(output.first);
            if (model_output == converted_model_.outputs.end()) {
                throw std::domain_error("Can't bind output " + output.first);
            }
            *output.second = model_output->second.rep;
        }
    }

private:
    std::map<std::string, Input<Halide::Buffer<>> *> model_inputs_;
    std::map<std::string, Output<Halide::Buffer<>> *> model_outputs_;
    Model converted_model_;
};
}  // namespace

HALIDE_REGISTER_GENERATOR(
    OnnxModelConverterGenerator, onnx_model_generator);
