#include "tflite_parser.h"

#include <algorithm>
#include <iostream>
#include <memory>

#include "app_util.h"
#include "ops.h"
#include "tflite_schema_generated.h"

namespace interpret_nn {

namespace {

#if (__cplusplus == 201103L || _MSVC_LANG == 201103L)

template<class T, class... Args>
std::unique_ptr<T> make_unique(Args &&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

#endif

tflite::BuiltinOperator GetBuiltinCode(const tflite::OperatorCode *op_code) {
    APP_CHECK(op_code != nullptr);

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

    static ActivationFunction ParseActivationFunction(
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
        }
    }

    static TensorType ParseType(tflite::TensorType t) {
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
        }
    }

    static Padding ParsePadding(tflite::Padding p) {
        switch (p) {
        case tflite::Padding_SAME:
            return Padding::Same;
        case tflite::Padding_VALID:
            return Padding::Valid;
        }
    }

    std::unique_ptr<Tensor> ParseTensor(const tflite::Tensor *t) {
        const auto *buffers = model_->buffers();
        std::vector<uint8_t> data;
        if (t->buffer() != 0) {
            auto buffer = buffers->Get(t->buffer())->data();
            if (buffer) {
                data.assign(buffer->cbegin(), buffer->cend());
            }
        }

        std::vector<halide_dimension_t> shape(t->shape()->size());
        for (int i = 0; i < (int)shape.size(); i++) {
            shape[i].min = 0;
            shape[i].extent = t->shape()->Get(shape.size() - 1 - i);
            shape[i].stride = 0;
        }
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
        return make_unique<Tensor>(
            t->name()->str(), ParseType(t->type()), std::move(shape),
            std::move(data), std::move(quantization));
    }

    std::unique_ptr<Op> ParseAdd(const tflite::Operator *op) {
        const auto options = op->builtin_options_as_AddOptions();
        Tensor *input1 = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *input2 = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<AddOp>(
            input1, input2, output,
            ParseActivationFunction(options->fused_activation_function()));
    }

    std::unique_ptr<Op> ParseAveragePool2D(const tflite::Operator *op) {
        const auto options = op->builtin_options_as_Pool2DOptions();
        Padding padding = ParsePadding(options->padding());
        std::vector<int> stride = {
            options->stride_w(),
            options->stride_h(),
        };
        std::vector<int> filter_size = {
            options->filter_width(),
            options->filter_height(),
        };
        ActivationFunction activation =
            ParseActivationFunction(options->fused_activation_function());
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<AveragePoolOp>(
            input, output, stride, filter_size, padding, activation);
    }

    std::unique_ptr<Op> ParseConv2D(const tflite::Operator *op) {
        const tflite::Conv2DOptions *options =
            op->builtin_options_as_Conv2DOptions();
        std::vector<int> dilation_factor = {
            options->dilation_w_factor(),
            options->dilation_h_factor(),
        };
        ActivationFunction activation =
            ParseActivationFunction(options->fused_activation_function());
        Padding padding = ParsePadding(options->padding());
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

    std::unique_ptr<Op> ParseDepthwiseConv2D(const tflite::Operator *op) {
        const tflite::DepthwiseConv2DOptions *options =
            op->builtin_options_as_DepthwiseConv2DOptions();
        std::vector<int> dilation_factor = {
            options->dilation_w_factor(),
            options->dilation_h_factor(),
        };
        int depth_multiplier = options->depth_multiplier();
        ActivationFunction activation =
            ParseActivationFunction(options->fused_activation_function());
        Padding padding = ParsePadding(options->padding());
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

    std::unique_ptr<Op> ParsePad(const tflite::Operator *op) {
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *padding = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<PadOp>(input, padding, output);
    }

    std::unique_ptr<Op> ParseReshape(const tflite::Operator *op) {
        const tflite::ReshapeOptions *options =
            op->builtin_options_as_ReshapeOptions();
        std::vector<int> new_shape(options->new_shape()->cbegin(),
                                   options->new_shape()->cend());
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return make_unique<ReshapeOp>(input, output, new_shape);
    }

    std::unique_ptr<Op> ParseOp(const tflite::Operator *op) {
        const auto *opcodes = model_->operator_codes();

        int opcode_index = op->opcode_index();
        const auto *opcode = opcodes->Get(opcode_index);

        std::string name;
        auto builtin_code = GetBuiltinCode(opcode);
        APP_CHECK(builtin_code != tflite::BuiltinOperator_CUSTOM);
        switch (builtin_code) {
        case tflite::BuiltinOperator_ADD:
            return ParseAdd(op);
        case tflite::BuiltinOperator_AVERAGE_POOL_2D:
            return ParseAveragePool2D(op);
        case tflite::BuiltinOperator_CONV_2D:
            return ParseConv2D(op);
        case tflite::BuiltinOperator_DEPTHWISE_CONV_2D:
            return ParseDepthwiseConv2D(op);
        case tflite::BuiltinOperator_PAD:
            return ParsePad(op);
        case tflite::BuiltinOperator_RESHAPE:
            return ParseReshape(op);
        default:
            APP_FATAL << "Unsupported op "
                      << tflite::EnumNameBuiltinOperator(builtin_code);
        }
    }

    Model Parse() {
        const auto &subgraphs = *model_->subgraphs();
        APP_CHECK(subgraphs.size() == 1) << "Only 1 subgraph is currently supported.";
        const tflite::SubGraph &subgraph = *subgraphs[0];

        for (const tflite::Tensor *t : *subgraph.tensors()) {
            result_.tensors.emplace_back(ParseTensor(t));
        }

        for (const tflite::Operator *i : *subgraph.operators()) {
            result_.ops.emplace_back(ParseOp(i));
        }

        return std::move(result_);
    }
};

}  // namespace

Model ParseTfLiteModel(const tflite::Model *model) {
    return Parser(model).Parse();
}

Model ParseTfLiteModelFromBuffer(const void *buffer) {
    return ParseTfLiteModel(tflite::GetModel(buffer));
}

}  // namespace interpret_nn
