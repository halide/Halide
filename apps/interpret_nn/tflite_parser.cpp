#include "tflite_parser.h"

#include <algorithm>
#include <iostream>
#include <memory>

#include "error_util.h"
#include "ops.h"
#include "tflite_schema_generated.h"

namespace interpret_nn {

namespace {

tflite::BuiltinOperator get_builtin_code(const tflite::OperatorCode *op_code) {
    return std::max(
        op_code->builtin_code(),
        static_cast<tflite::BuiltinOperator>(op_code->deprecated_builtin_code()));
}

class Parser {
    const tflite::Model *model_;
    Model result_;

public:
    explicit Parser(const tflite::Model *model)
        : model_(model) {
    }

    static ActivationFunction parse_activation_function(
        tflite::ActivationFunctionType f) {
        switch (f) {
        case tflite::ActivationFunctionType_NONE:
            return ActivationFunction::None;
        case tflite::ActivationFunctionType_RELU:
            return ActivationFunction::Relu;
        case tflite::ActivationFunctionType_RELU_N1_TO_1:
            return ActivationFunction::ReluN1To1;
        case tflite::ActivationFunctionType_RELU6:
            return ActivationFunction::Relu6;
        case tflite::ActivationFunctionType_TANH:
            return ActivationFunction::Tanh;
        case tflite::ActivationFunctionType_SIGN_BIT:
            return ActivationFunction::SignBit;
        default:
            LOG_FATAL << "Unknown tflite::ActivationFunctionType";
        }
    }

    static TensorType parse_type(tflite::TensorType t) {
        switch (t) {
        case tflite::TensorType_FLOAT32:
            return TensorType::Float32;
        case tflite::TensorType_FLOAT16:
            return TensorType::Float16;
        case tflite::TensorType_INT32:
            return TensorType::Int32;
        case tflite::TensorType_UINT8:
            return TensorType::UInt8;
        case tflite::TensorType_INT64:
            return TensorType::Int64;
        case tflite::TensorType_STRING:
            return TensorType::String;
        case tflite::TensorType_BOOL:
            return TensorType::Bool;
        case tflite::TensorType_INT16:
            return TensorType::Int16;
        case tflite::TensorType_COMPLEX64:
            return TensorType::Complex64;
        case tflite::TensorType_INT8:
            return TensorType::Int8;
        case tflite::TensorType_FLOAT64:
            return TensorType::Float64;
        case tflite::TensorType_COMPLEX128:
            return TensorType::Complex128;
        case tflite::TensorType_UINT64:
            return TensorType::UInt64;
        default:
            LOG_FATAL << "Unknown tflite::TensorType";
        }
    }

    static Padding parse_padding(tflite::Padding p) {
        switch (p) {
        case tflite::Padding_SAME:
            return Padding::Same;
        case tflite::Padding_VALID:
            return Padding::Valid;
        default:
            LOG_FATAL << "Unknown tflite::Padding";
        }
    }

    std::unique_ptr<Tensor> parse_tensor(const tflite::Tensor *t) {
        const auto *buffers = model_->buffers();
        std::vector<uint8_t> data;
        if (t->buffer() != 0) {
            auto buffer = buffers->Get(t->buffer())->data();
            if (buffer) {
                data.assign(buffer->cbegin(), buffer->cend());
            }
        }

        std::vector<halide_dimension_t> shape(t->shape()->size());
        size_t shape_size = 1;
        for (int i = 0; i < (int)shape.size(); i++) {
            shape[i].min = 0;
            shape[i].extent = t->shape()->Get(shape.size() - 1 - i);
            shape[i].stride = shape_size;
            shape_size *= shape[i].extent;
        }

        TensorType type = parse_type(t->type());
        assert(data.empty() || data.size() == shape_size * sizeof_tensor_type(type));

        QuantizationInfo quantization;
        if (t->quantization()) {
            quantization.dimension =
                shape.size() - t->quantization()->quantized_dimension();
            if (t->quantization()->scale()) {
                quantization.scale.assign(t->quantization()->scale()->cbegin(),
                                          t->quantization()->scale()->cend());
            }
            if (t->quantization()->zero_point()) {
                quantization.zero.assign(t->quantization()->zero_point()->cbegin(),
                                         t->quantization()->zero_point()->cend());
            }
        }

        if (type == TensorType::Int8) {
            // Convert Int8 buffers to UInt8 buffers by adjusting the quantization info.
            // TODO: Is this correct??
            type = TensorType::UInt8;
            if (quantization.scale.size() == 0) {
                quantization.scale.push_back(1);
            }
            if (quantization.zero.size() == 0) {
                quantization.zero.push_back(128);
            } else {
                for (int &i : quantization.zero) {
                    i = 128 + i;
                }
            }
        }

        return make_unique<Tensor>(
            t->name()->str(), type, std::move(shape),
            std::move(data), std::move(quantization));
    }

    std::unique_ptr<Op> parse_add(const tflite::Operator *op) {
        const auto options = op->builtin_options_as_AddOptions();
        Tensor *input1 = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *input2 = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<AddOp>(
            input1, input2, output,
            parse_activation_function(options->fused_activation_function()));
    }

    std::unique_ptr<Op> parse_average_pool2D(const tflite::Operator *op) {
        const auto options = op->builtin_options_as_Pool2DOptions();
        Padding padding = parse_padding(options->padding());
        std::vector<int> stride = {
            options->stride_w(),
            options->stride_h(),
        };
        std::vector<int> filter_size = {
            options->filter_width(),
            options->filter_height(),
        };
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<AveragePoolOp>(
            input, output, stride, filter_size, padding, activation);
    }

    std::unique_ptr<Op> parse_concatenation(const tflite::Operator *op) {
        const tflite::ConcatenationOptions *options =
            op->builtin_options_as_ConcatenationOptions();
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        std::vector<Tensor *> inputs;
        for (auto i = op->inputs()->cbegin(); i != op->inputs()->cend(); ++i) {
            inputs.push_back(result_.tensors[*i].get());
        }
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<ConcatenationOp>(inputs, output, options->axis(), activation);
    }

    std::unique_ptr<Op> parse_conv2D(const tflite::Operator *op) {
        const tflite::Conv2DOptions *options =
            op->builtin_options_as_Conv2DOptions();
        std::vector<int> dilation_factor = {
            options->dilation_w_factor(),
            options->dilation_h_factor(),
        };
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        Padding padding = parse_padding(options->padding());
        std::vector<int> stride = {
            options->stride_w(),
            options->stride_h(),
        };
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *filter = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *bias = result_.tensors[op->inputs()->Get(2)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<Conv2DOp>(input, filter, bias, output, stride,
                                     dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> parse_depthwise_conv2D(const tflite::Operator *op) {
        const tflite::DepthwiseConv2DOptions *options =
            op->builtin_options_as_DepthwiseConv2DOptions();
        std::vector<int> dilation_factor = {
            options->dilation_w_factor(),
            options->dilation_h_factor(),
        };
        int depth_multiplier = options->depth_multiplier();
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        Padding padding = parse_padding(options->padding());
        std::vector<int> stride = {
            options->stride_w(),
            options->stride_h(),
        };
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *filter = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *bias = result_.tensors[op->inputs()->Get(2)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<DepthwiseConv2DOp>(
            input, filter, bias, output, depth_multiplier, stride, dilation_factor,
            padding, activation);
    }

    std::unique_ptr<Op> parse_pad(const tflite::Operator *op) {
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *padding = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<PadOp>(input, padding, output);
    }

    std::unique_ptr<Op> parse_reshape(const tflite::Operator *op) {
        const tflite::ReshapeOptions *options =
            op->builtin_options_as_ReshapeOptions();
        std::vector<int> new_shape;
        if (options) {
            new_shape.assign(options->new_shape()->cbegin(), options->new_shape()->cend());
        }
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<ReshapeOp>(input, output, new_shape);
    }

    std::unique_ptr<Op> parse_quantize(const tflite::Operator *op) {
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<QuantizeOp>(input, output);
    }

    std::unique_ptr<Op> parse_op(const tflite::Operator *op) {
        const auto *opcodes = model_->operator_codes();

        int opcode_index = op->opcode_index();
        const auto *opcode = opcodes->Get(opcode_index);

        auto builtin_code = get_builtin_code(opcode);
        switch (builtin_code) {
        case tflite::BuiltinOperator_ADD:
            return parse_add(op);
        case tflite::BuiltinOperator_AVERAGE_POOL_2D:
            return parse_average_pool2D(op);
        case tflite::BuiltinOperator_CONCATENATION:
            return parse_concatenation(op);
        case tflite::BuiltinOperator_CONV_2D:
            return parse_conv2D(op);
        case tflite::BuiltinOperator_DEPTHWISE_CONV_2D:
            return parse_depthwise_conv2D(op);
        case tflite::BuiltinOperator_PAD:
            return parse_pad(op);
        case tflite::BuiltinOperator_RESHAPE:
            return parse_reshape(op);
        case tflite::BuiltinOperator_QUANTIZE:
            return parse_quantize(op);
        default:
            LOG_FATAL << "Unsupported op "
                      << tflite::EnumNameBuiltinOperator(builtin_code);
        }
    }

    Model parse() {
        const auto &subgraphs = *model_->subgraphs();
        CHECK(subgraphs.size() == 1) << "Only 1 subgraph is currently supported.";
        const tflite::SubGraph &subgraph = *subgraphs[0];

        for (const tflite::Tensor *t : *subgraph.tensors()) {
            result_.tensors.emplace_back(parse_tensor(t));
        }

        for (const tflite::Operator *i : *subgraph.operators()) {
            result_.ops.emplace_back(parse_op(i));
        }

        for (int i : *subgraph.inputs()) {
            result_.tensors[i]->set_input(true);
        }
        for (int i : *subgraph.outputs()) {
            result_.tensors[i]->set_output(true);
        }

        return std::move(result_);
    }

    // Movable but not copyable.
    Parser() = delete;
    Parser(const Parser &) = delete;
    Parser &operator=(const Parser &) = delete;
    Parser(Parser &&) = default;
    Parser &operator=(Parser &&) = default;
};

}  // namespace

Model parse_tflite_model(const tflite::Model *model) {
    return Parser(model).parse();
}

Model parse_tflite_model_from_buffer(const void *buffer) {
    return parse_tflite_model(tflite::GetModel(buffer));
}

}  // namespace interpret_nn
