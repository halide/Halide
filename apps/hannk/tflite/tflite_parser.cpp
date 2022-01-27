#include "tflite/tflite_parser.h"

#include <algorithm>
#include <memory>

#include "interpreter/lower.h"
#include "interpreter/ops.h"
#include "tensorflow/lite/schema/schema_generated.h"
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
    std::vector<TensorPtr> tensors_;
    std::vector<std::unique_ptr<OpGroup>> subgraphs_;

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
            HCHECK(0) << "Unknown tflite::ActivationFunctionType";
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
            HCHECK(0) << "Unhandled type in ConvertTfLiteType";
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
            HCHECK(0) << "Unknown tflite::Padding";
        }
    }

    TensorPtr parse_tensor(const tflite::Tensor *t) {
        std::vector<int> shape;
        if (t->shape()) {
            const int shape_size = t->shape()->size();
            shape.reserve(shape_size);
            for (int i = 0; i < shape_size; i++) {
                shape.push_back(t->shape()->Get(shape_size - 1 - i));
            }
        }
        // HCHECK(t->shape()) << "Dynamic shapes not supported.";

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

                auto p = std::make_shared<Tensor>(t->name()->str(), std::move(buffer), std::move(quantization));
                p->set_constant();
                return p;
            }
        }

        // Create an "unallocated" Buffer, which points to null.
        HalideBuffer<void> buffer(type, nullptr, shape);
        return std::make_shared<Tensor>(t->name()->str(), std::move(buffer), std::move(quantization));
    }

    OpPtr parse_binary(const tflite::Operator *op, BinaryOp::Operator type, bool swap_operands = false) {
        TensorPtr a = tensors_[op->inputs()->Get(0)];
        TensorPtr b = tensors_[op->inputs()->Get(1)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        if (swap_operands) {
            std::swap(a, b);
        }
        return make_op<BinaryOp>(a, b, output, type, ActivationFunction::None);
    }

// We can't do this with templates...
#define PARSE_BINARY_WITH_ACTIVATION(op, Op) \
    make_op<BinaryOp>(                       \
        tensors_[op->inputs()->Get(0)],      \
        tensors_[op->inputs()->Get(1)],      \
        tensors_[op->outputs()->Get(0)],     \
        BinaryOp::Op,                        \
        parse_activation_function(op->builtin_options_as_##Op##Options()->fused_activation_function()));

    OpPtr parse_pool2D(const tflite::Operator *op, Pool2DOp::Operator reduce_op) {
        const auto options = op->builtin_options_as_Pool2DOptions();
        Padding padding = parse_padding(options->padding());
        std::array<int, 2> stride = {{
            options->stride_w(),
            options->stride_h(),
        }};
        std::array<int, 2> filter_size = {{
            options->filter_width(),
            options->filter_height(),
        }};
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<Pool2DOp>(
            input, output, stride, filter_size, padding, reduce_op, activation);
    }

    OpPtr parse_concatenation(const tflite::Operator *op) {
        const tflite::ConcatenationOptions *options =
            op->builtin_options_as_ConcatenationOptions();
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        HCHECK(activation == ActivationFunction::None);
        std::vector<TensorPtr> inputs;
        for (auto i = op->inputs()->cbegin(); i != op->inputs()->cend(); ++i) {
            inputs.push_back(tensors_[*i]);
        }
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        int axis = options->axis();
        // Handle negative values, which are legal
        if (axis < 0) {
            axis = (int)output->rank() + axis;
        }
        // Now 'flip' the axis so that it refers to the right dimension in
        // the Tensor (since we reverse the dimension order)
        axis = (int)output->rank() - axis - 1;
        return make_op<ConcatenationOp>(inputs, output, axis);
    }

    OpPtr parse_conv2D(const tflite::Operator *op) {
        const tflite::Conv2DOptions *options =
            op->builtin_options_as_Conv2DOptions();
        std::array<int, 2> dilation_factor = {{
            options->dilation_w_factor(),
            options->dilation_h_factor(),
        }};
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        Padding padding = parse_padding(options->padding());
        std::array<int, 2> stride = {{
            options->stride_w(),
            options->stride_h(),
        }};
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr filter = tensors_[op->inputs()->Get(1)];
        TensorPtr bias = tensors_[op->inputs()->Get(2)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<ConvOp>(input, filter, bias, output, stride,
                               dilation_factor, padding, activation);
    }

    OpPtr parse_depthwise_conv2D(const tflite::Operator *op) {
        const tflite::DepthwiseConv2DOptions *options =
            op->builtin_options_as_DepthwiseConv2DOptions();
        std::array<int, 2> dilation_factor = {{
            options->dilation_w_factor(),
            options->dilation_h_factor(),
        }};
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        Padding padding = parse_padding(options->padding());
        std::array<int, 2> stride = {{
            options->stride_w(),
            options->stride_h(),
        }};
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr filter = tensors_[op->inputs()->Get(1)];
        TensorPtr bias = tensors_[op->inputs()->Get(2)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        int depth_multiplier = output->extent(0) / input->extent(0);
        return make_op<DepthwiseConv2DOp>(
            input, filter, bias, output, depth_multiplier,
            stride, dilation_factor, padding, activation);
    }

    OpPtr parse_fully_connected(const tflite::Operator *op) {
        const tflite::FullyConnectedOptions *options =
            op->builtin_options_as_FullyConnectedOptions();
        ActivationFunction activation =
            parse_activation_function(options->fused_activation_function());
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr filter = tensors_[op->inputs()->Get(1)];
        TensorPtr bias = tensors_[op->inputs()->Get(2)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return lower_tflite_fullyconnected(input, filter, bias, output, activation);
    }

    OpPtr parse_pad(const tflite::Operator *op) {
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr padding = tensors_[op->inputs()->Get(1)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<PadOp>(input, padding, output);
    }

    OpPtr parse_reshape(const tflite::Operator *op) {
        const tflite::ReshapeOptions *options =
            op->builtin_options_as_ReshapeOptions();
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        // If there are two inputs, and the second is an int32 vector, it should
        // be used to specify the new shape (instead of ReshapeOptions).
        TensorPtr shape_tensor = nullptr;
        if (op->inputs()->size() == 2) {
            shape_tensor = tensors_[op->inputs()->Get(1)];
        } else if (options) {
            size_t size = options->new_shape()->size();
            HalideBuffer<int32_t, 1> shape_data(size);
            for (size_t i = 0; i < size; i++) {
                shape_data(i) = options->new_shape()->Get(i);
            }
            shape_tensor = std::make_shared<Tensor>(input->name() + "_shape", shape_data);
            shape_tensor->set_constant();
        }
        return make_op<ReshapeOp>(input, shape_tensor, output);
    }

    OpPtr parse_shape(const tflite::Operator *op) {
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<ShapeOp>(input, output);
    }

    OpPtr parse_gather(const tflite::Operator *op) {
        const tflite::GatherOptions *options =
            op->builtin_options_as_GatherOptions();
        int axis = options->axis();
        int batch_dims = options->batch_dims();
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr indices = tensors_[op->inputs()->Get(1)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        if (axis < 0) {
            axis += input->rank();
        }
        axis = input->rank() - 1 - axis;
        return make_op<GatherOp>(input, indices, output, axis, batch_dims);
    }

    OpPtr parse_space_to_depth(const tflite::Operator *op) {
        const tflite::SpaceToDepthOptions *options =
            op->builtin_options_as_SpaceToDepthOptions();
        int block_size = options->block_size();
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<SpaceDepthOp>(input, output, block_size);
    }

    OpPtr parse_depth_to_space(const tflite::Operator *op) {
        const tflite::DepthToSpaceOptions *options =
            op->builtin_options_as_DepthToSpaceOptions();
        int block_size = options->block_size();
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<SpaceDepthOp>(input, output, -block_size);
    }

    OpPtr parse_split(const tflite::Operator *op, int axis_tensor_index, int input_tensor_index) {
        assert(axis_tensor_index < (int)op->inputs()->size());
        TensorPtr axis_tensor = tensors_[op->inputs()->Get(axis_tensor_index)];
        HCHECK(axis_tensor->is_allocated()) << "Can't handle dynamic axis for Split.\n";
        int axis = axis_tensor->buffer<int32_t>()();

        assert(input_tensor_index < (int)op->inputs()->size());
        TensorPtr input = tensors_[op->inputs()->Get(input_tensor_index)];
        std::vector<TensorPtr> outputs;
        for (auto i = op->outputs()->cbegin(); i != op->outputs()->cend(); ++i) {
            outputs.push_back(tensors_[*i]);
        }
        // Handle negative values, which are legal
        if (axis < 0) {
            axis = (int)input->rank() + axis;
        }
        // Now 'flip' the axis so that it refers to the right dimension in
        // the Tensor (since we reverse the dimension order)
        axis = (int)input->rank() - axis - 1;
        return make_op<SplitOp>(input, outputs, axis);
    }

    OpPtr parse_split(const tflite::Operator *op) {
        return parse_split(op, 0, 1);
    }

    OpPtr parse_split_v(const tflite::Operator *op) {
        return parse_split(op, 2, 0);
    }

    OpPtr parse_softmax(const tflite::Operator *op) {
        const tflite::SoftmaxOptions *options =
            op->builtin_options_as_SoftmaxOptions();
        float beta = options->beta();
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        const int axis = 0;  // In TFLite, normalization is always against the first axis.
        return make_op<SoftmaxOp>(input, output, beta, axis);
    }

    OpPtr parse_l2_normalization(const tflite::Operator *op) {
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        const int axis = 0;  // In TFLite, normalization is always against the first axis.
        return make_op<L2NormalizationOp>(input, output, axis);
    }

    OpPtr parse_reduction(const tflite::Operator *op, ReductionOp::Operator reduction_op) {
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr indices = tensors_[op->inputs()->Get(1)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
#ifndef NDEBUG
        const tflite::ReducerOptions *options = op->builtin_options_as_ReducerOptions();
        const bool keep_dims = options ? options->keep_dims() : false;
        // TODO: I have yet to find any examples of keep_dims == false in the wild.
        // If/when we do, handle it appropriately.
        assert(keep_dims == true);
#endif
        return make_op<ReductionOp>(reduction_op, input, indices, output);
    }

    OpPtr parse_unary(const tflite::Operator *op, UnaryOp::Operator type) {
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<UnaryOp>(input, output, type);
    }

    OpPtr parse_lstm(const tflite::Operator *op) {
        TensorPtr data_input = tensors_[op->inputs()->Get(0)];
        TensorPtr prev_activ_input = tensors_[op->inputs()->Get(1)];
        TensorPtr weights_input = tensors_[op->inputs()->Get(2)];
        TensorPtr biases_input = tensors_[op->inputs()->Get(3)];
        TensorPtr prev_state_input = tensors_[op->inputs()->Get(4)];

        TensorPtr activ_output = tensors_[op->outputs()->Get(0)];
        TensorPtr state_output = tensors_[op->outputs()->Get(1)];
        TensorPtr concat_temp = tensors_[op->outputs()->Get(2)];
        TensorPtr activ_temp = tensors_[op->outputs()->Get(3)];

        // TODO: there is an activation function specified here but it's not clear
        // whether it's used in the LSTM reference implementation. Ignoring for now.
        //
        // const auto options = op->builtin_options_as_LSTMOptions();
        // ActivationFunction activation = parse_activation_function(options->fused_activation_function());

        const ActivationFunction activation = ActivationFunction::None;

        return lower_tflite_lstm(data_input, prev_activ_input, weights_input, biases_input, prev_state_input,
                                 activ_output, state_output, concat_temp, activ_temp, activation);
    }

    OpPtr parse_transpose(const tflite::Operator *op) {
        TensorPtr input = tensors_[op->inputs()->Get(0)];
        TensorPtr dims = tensors_[op->inputs()->Get(1)];
        TensorPtr output = tensors_[op->outputs()->Get(0)];
        return make_op<TransposeOp>(input, dims, output);
    }

    OpPtr parse_op(const tflite::Operator *op) {
        const auto *opcodes = model_->operator_codes();

        int opcode_index = op->opcode_index();
        const auto *opcode = opcodes->Get(opcode_index);

        auto builtin_code = get_builtin_code(opcode);
        switch (builtin_code) {
        case tflite::BuiltinOperator_ADD:
            return PARSE_BINARY_WITH_ACTIVATION(op, Add);
        case tflite::BuiltinOperator_AVERAGE_POOL_2D:
            return parse_pool2D(op, Pool2DOp::Average);
        case tflite::BuiltinOperator_CONCATENATION:
            return parse_concatenation(op);
        case tflite::BuiltinOperator_CONV_2D:
            return parse_conv2D(op);
        case tflite::BuiltinOperator_DEPTH_TO_SPACE:
            return parse_depth_to_space(op);
        case tflite::BuiltinOperator_DEPTHWISE_CONV_2D:
            return parse_depthwise_conv2D(op);
        case tflite::BuiltinOperator_EQUAL:
            return parse_binary(op, BinaryOp::Equal);
        case tflite::BuiltinOperator_FULLY_CONNECTED:
            return parse_fully_connected(op);
        case tflite::BuiltinOperator_GATHER:
            return parse_gather(op);
        // TODO: support GATHER_ND once we find a testcase for it
        // case tflite::BuiltinOperator_GATHER_ND:
        //     return parse_gather_nd(op);
        case tflite::BuiltinOperator_GREATER:
            return parse_binary(op, BinaryOp::LessEqual, true);
        case tflite::BuiltinOperator_GREATER_EQUAL:
            return parse_binary(op, BinaryOp::Less, true);
        case tflite::BuiltinOperator_L2_NORMALIZATION:
            return parse_l2_normalization(op);
        case tflite::BuiltinOperator_LESS:
            return parse_binary(op, BinaryOp::Less);
        case tflite::BuiltinOperator_LESS_EQUAL:
            return parse_binary(op, BinaryOp::LessEqual);
        case tflite::BuiltinOperator_LOGISTIC:
            return parse_unary(op, UnaryOp::Logistic);
        case tflite::BuiltinOperator_LSTM:
            return parse_lstm(op);
        case tflite::BuiltinOperator_MAX_POOL_2D:
            return parse_pool2D(op, Pool2DOp::Max);
        case tflite::BuiltinOperator_MEAN:
            return parse_reduction(op, ReductionOp::Mean);
        case tflite::BuiltinOperator_MUL:
            return PARSE_BINARY_WITH_ACTIVATION(op, Mul);
        case tflite::BuiltinOperator_NEG:
            return parse_unary(op, UnaryOp::Negate);
        case tflite::BuiltinOperator_NOT_EQUAL:
            return parse_binary(op, BinaryOp::NotEqual);
        case tflite::BuiltinOperator_PAD:
            return parse_pad(op);
        case tflite::BuiltinOperator_RELU:
            return parse_unary(op, UnaryOp::Relu);
        case tflite::BuiltinOperator_RELU6:
            return parse_unary(op, UnaryOp::Relu6);
        case tflite::BuiltinOperator_RELU_N1_TO_1:
            return parse_unary(op, UnaryOp::ReluN1To1);
        case tflite::BuiltinOperator_RESHAPE:
            return parse_reshape(op);
        case tflite::BuiltinOperator_SHAPE:
            return parse_shape(op);
        case tflite::BuiltinOperator_SOFTMAX:
            return parse_softmax(op);
        case tflite::BuiltinOperator_SPACE_TO_DEPTH:
            return parse_space_to_depth(op);
        case tflite::BuiltinOperator_SPLIT:
            return parse_split(op);
        case tflite::BuiltinOperator_SPLIT_V:
            return parse_split_v(op);
        case tflite::BuiltinOperator_SQUARE:
            return parse_unary(op, UnaryOp::Square);
        case tflite::BuiltinOperator_SUB:
            return PARSE_BINARY_WITH_ACTIVATION(op, Sub);
        case tflite::BuiltinOperator_TANH:
            return parse_unary(op, UnaryOp::Tanh);
        case tflite::BuiltinOperator_TRANSPOSE:
            return parse_transpose(op);

        default:
            HCHECK(0) << "Unsupported op "
                      << tflite::EnumNameBuiltinOperator(builtin_code);
        }
    }

    std::unique_ptr<OpGroup> parse_subgraph(const tflite::SubGraph *subgraph) {
        std::vector<TensorPtr> old_tensors;
        std::swap(old_tensors, tensors_);

        for (const tflite::Tensor *t : *subgraph->tensors()) {
            tensors_.emplace_back(parse_tensor(t));
        }

        std::vector<OpPtr> ops;
        for (const tflite::Operator *i : *subgraph->operators()) {
            ops.emplace_back(parse_op(i));
        }

        std::vector<TensorPtr> inputs;
        for (int i : *subgraph->inputs()) {
            inputs.push_back(tensors_[i]);
        }
        std::vector<TensorPtr> outputs;
        for (int i : *subgraph->outputs()) {
            outputs.push_back(tensors_[i]);
        }

        std::swap(tensors_, old_tensors);

        return make_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));
    }

    std::unique_ptr<OpGroup> parse() {
        for (const tflite::SubGraph *s : *model_->subgraphs()) {
            subgraphs_.push_back(parse_subgraph(s));
        }

        HCHECK(subgraphs_.size() == 1) << "Zero or multiple entry points found.";
        return std::move(subgraphs_.front());
    }

    // Movable but not copyable.
    Parser() = delete;
    Parser(const Parser &) = delete;
    Parser &operator=(const Parser &) = delete;
    Parser(Parser &&) = default;
    Parser &operator=(Parser &&) = default;
};

}  // namespace

std::unique_ptr<OpGroup> parse_tflite_model(const tflite::Model *model) {
    return Parser(model).parse();
}

std::unique_ptr<OpGroup> parse_tflite_model_from_buffer(const void *buffer) {
    return parse_tflite_model(tflite::GetModel(buffer));
}

}  // namespace hannk
