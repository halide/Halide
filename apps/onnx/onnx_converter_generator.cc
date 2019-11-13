#include "Halide.h"
#include "onnx_converter.h"
#include <fstream>
#include <map>

namespace {

class OnnxModelConverterGenerator
    : public Halide::Generator<OnnxModelConverterGenerator> {
public:
    GeneratorParam<std::string> model_file_path{"model_file_path", ""};

    void configure() {
        onnx::ModelProto onnx_model;
        std::fstream input(model_file_path.value(), std::ios::in | std::ios::binary);
        if (!input) {
            std::cerr << "Can't read model file" << model_file_path.value() << "\n";
            abort();
        }
        std::stringstream buffer;
        buffer << input.rdbuf();
        if (!onnx_model.ParseFromString(buffer.str())) {
            std::cerr << "Can't parse model file" << model_file_path.value() << "\n";
            abort();
        }

        std::unordered_map<std::string, int> expected_dim_sizes;
        converted_model_ = convert_model(onnx_model, expected_dim_sizes, IOLayout::Native);
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
                std::cerr << "Can't bind input " << input.first;
                abort();
            }
            tensor->second.rep = *input.second;
        }
        for (auto &output : model_outputs_) {
            auto model_output = converted_model_.outputs.find(output.first);
            if (model_output == converted_model_.outputs.end()) {
                std::cerr << "Can't bind output " << output.first << "\n";
                abort();
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
