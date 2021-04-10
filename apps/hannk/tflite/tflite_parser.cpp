#include "tflite/tflite_parser.h"

#include <algorithm>
#include <memory>

#include "interpreter/ops.h"
#include "tflite_schema_generated.h"
#include "util/error_util.h"

namespace hannk {

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
            CHECK(0) << "Unknown tflite::ActivationFunctionType";
        }
    }

    static halide_type_t parse_type(tflite::TensorType t) {
        switch (t) {
        case tflite::TensorType_BOOL:
            return halide_type_t(halide_type_uint, 1);
        case tflite::TensorType_FLOAT16:
            return halide_type_t(halide_type_float, 16);
        case tflite::TensorType_FLOAT32:
            return halide_type_t(halide_type_float, 32);
        case tflite::TensorType_FLOAT64:
            return halide_type_t(halide_type_float, 64);
        case tflite::TensorType_INT16:
            return halide_type_t(halide_type_int, 16);
        case tflite::TensorType_INT32:
            return halide_type_t(halide_type_int, 32);
        case tflite::TensorType_INT64:
            return halide_type_t(halide_type_int, 64);
        case tflite::TensorType_INT8:
            return halide_type_t(halide_type_int, 8);
        case tflite::TensorType_UINT8:
            return halide_type_t(halide_type_uint, 8);

        case tflite::TensorType_STRING:
        case tflite::TensorType_COMPLEX64:
        case tflite::TensorType_COMPLEX128:
        default:
            CHECK(0) << "Unhandled type in ConvertTfLiteType";
            return halide_type_t();
        }
    }

    static Padding parse_padding(tflite::Padding p) {
        switch (p) {
        case tflite::Padding_SAME:
            return Padding::Same;
        case tflite::Padding_VALID:
            return Padding::Valid;
        default:
            CHECK(0) << "Unknown tflite::Padding";
        }
    }

    std::unique_ptr<Tensor> parse_tensor(const tflite::Tensor *t) {
        CHECK(t->shape()) << "Dynamic shapes not supported.";
        const int shape_size = t->shape()->size();
        std::vector<int> shape;
        shape.reserve(shape_size);
        for (int i = 0; i < shape_size; i++) {
            shape.push_back(t->shape()->Get(shape_size - 1 - i));
        }

        halide_type_t type = parse_type(t->type());

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

        if (t->buffer() != 0) {
            const auto *tflite_buffer = model_->buffers()->Get(t->buffer())->data();
            if (tflite_buffer) {
                // tflite_buffer->Data() points at read-only data in the flatbuffer.
                // Construct a HalideBuffer that points to it (but does not copy or own it).
                const void *data = static_cast<const void *>(tflite_buffer->Data());
                assert(data);
                HalideBuffer<void> buffer(type, const_cast<void *>(data), shape);
                assert(tflite_buffer->size() == buffer.size_in_bytes());

                return ::hannk::make_unique<Tensor>(t->name()->str(), std::move(buffer), std::move(quantization));
            }
        }

        // Create an "unallocated" Buffer, which points to null.
        HalideBuffer<void> buffer(type, nullptr, shape);
        return ::hannk::make_unique<Tensor>(t->name()->str(), std::move(buffer), std::move(quantization));
    }

    std::unique_ptr<Op> parse_binary(const tflite::Operator *op, BinaryOp::Operator type) {
        Tensor *a = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *b = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<BinaryOp>(a, b, output, type, ActivationFunction::None);
    }

// We can't do this with templates...
#define PARSE_BINARY_WITH_ACTIVATION(op, Op)          \
    ::hannk::make_unique<BinaryOp>(                   \
        result_.tensors[op->inputs()->Get(0)].get(),  \
        result_.tensors[op->inputs()->Get(1)].get(),  \
        result_.tensors[op->outputs()->Get(0)].get(), \
        BinaryOp::Op,                                 \
        parse_activation_function(op->builtin_options_as_##Op##Options()->fused_activation_function()));

    std::unique_ptr<Op> parse_pool2D(const tflite::Operator *op, PoolOp::Operator reduce_op) {
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
        return ::hannk::make_unique<PoolOp>(
            input, output, stride, filter_size, padding, reduce_op, activation);
    }

    std::unique_ptr<Op> parse_concatenation(const tflite::Operator *op) {
        const tflite::ConcatenationOptions *options =
            op->builtin_options_as_ConcatenationOptions();
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        CHECK(activation == ActivationFunction::None);
        std::vector<Tensor *> inputs;
        for (auto i = op->inputs()->cbegin(); i != op->inputs()->cend(); ++i) {
            inputs.push_back(result_.tensors[*i].get());
        }
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        int axis = options->axis();
        // Handle negative values, which are legal
        if (axis < 0) {
            axis = (int)output->rank() + axis;
        }
        // Now 'flip' the axis so that it refers to the right dimension in
        // the Tensor (since we reverse the dimension order)
        axis = (int)output->rank() - axis - 1;
        return ::hannk::make_unique<ConcatenationOp>(inputs, output, axis);
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
        return ::hannk::make_unique<Conv2DOp>(input, filter, bias, output, stride,
                                              dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> parse_depthwise_conv2D(const tflite::Operator *op) {
        const tflite::DepthwiseConv2DOptions *options =
            op->builtin_options_as_DepthwiseConv2DOptions();
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
        int depth_multiplier = output->extent(0) / input->extent(0);
        return ::hannk::make_unique<DepthwiseConv2DOp>(
            input, filter, bias, output, depth_multiplier,
            stride, dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> parse_fully_connected(const tflite::Operator *op) {
        const tflite::FullyConnectedOptions *options =
            op->builtin_options_as_FullyConnectedOptions();
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *filter = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *bias = result_.tensors[op->inputs()->Get(2)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<FullyConnectedOp>(input, filter, bias, output, activation);
    }

    std::unique_ptr<Op> parse_pad(const tflite::Operator *op) {
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *padding = result_.tensors[op->inputs()->Get(1)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<PadOp>(input, padding, output);
    }

    std::unique_ptr<Op> parse_reshape(const tflite::Operator *op) {
        const tflite::ReshapeOptions *options =
            op->builtin_options_as_ReshapeOptions();
        std::vector<int> new_shape;
        // If there are two inputs, and the second is an int32 vector, it should
        // be used to specify the new shape (instead of ReshapeOptions).
        CHECK(options) << "Dynamic reshape not supported\n";
        if (options) {
            new_shape.assign(options->new_shape()->cbegin(), options->new_shape()->cend());
        }
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<ReshapeOp>(input, output, new_shape);
    }

    std::unique_ptr<Op> parse_softmax(const tflite::Operator *op) {
        const tflite::SoftmaxOptions *options =
            op->builtin_options_as_SoftmaxOptions();
        float beta = options->beta();
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<SoftmaxOp>(input, output, beta);
    }

    std::unique_ptr<Op> parse_l2_normalization(const tflite::Operator *op) {
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<L2NormalizationOp>(input, output);
    }

    std::unique_ptr<Op> parse_unary(const tflite::Operator *op, UnaryOp::Operator type) {
        Tensor *input = result_.tensors[op->inputs()->Get(0)].get();
        Tensor *output = result_.tensors[op->outputs()->Get(0)].get();
        return ::hannk::make_unique<UnaryOp>(input, output, type);
    }

    std::unique_ptr<Op> parse_op(const tflite::Operator *op) {
        const auto *opcodes = model_->operator_codes();

        int opcode_index = op->opcode_index();
        const auto *opcode = opcodes->Get(opcode_index);

        auto builtin_code = get_builtin_code(opcode);
        switch (builtin_code) {
        case tflite::BuiltinOperator_ADD:
            return PARSE_BINARY_WITH_ACTIVATION(op, Add);
        case tflite::BuiltinOperator_SUB:
            return PARSE_BINARY_WITH_ACTIVATION(op, Sub);
        case tflite::BuiltinOperator_AVERAGE_POOL_2D:
            return parse_pool2D(op, PoolOp::Average);
        case tflite::BuiltinOperator_MAX_POOL_2D:
            return parse_pool2D(op, PoolOp::Max);
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
        case tflite::BuiltinOperator_FULLY_CONNECTED:
            return parse_fully_connected(op);
        case tflite::BuiltinOperator_SOFTMAX:
            return parse_softmax(op);
        case tflite::BuiltinOperator_L2_NORMALIZATION:
            return parse_l2_normalization(op);
        case tflite::BuiltinOperator_LOGISTIC:
            return parse_unary(op, UnaryOp::Logistic);

        default:
            CHECK(0) << "Unsupported op "
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

}  // namespace hannk
