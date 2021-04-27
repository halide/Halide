#include "delegate/hannk_delegate.h"

#include <memory>
#include <string>
#include <vector>

#include "interpreter/interpreter.h"
#include "interpreter/lower.h"
#include "interpreter/ops.h"
#include "tensorflow/lite/builtin_ops.h"
#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/c/c_api.h"
#include "util/error_util.h"

// Use a List-Of-X approach here to ensure that places we handle ops are kept in sync
#define ALL_KNOWN_OPS         \
    KNOWN_OP(Add)             \
    KNOWN_OP(AveragePool2d)   \
    KNOWN_OP(Concatenation)   \
    KNOWN_OP(Conv2d)          \
    KNOWN_OP(DepthToSpace)    \
    KNOWN_OP(DepthwiseConv2d) \
    KNOWN_OP(Equal)           \
    KNOWN_OP(FullyConnected)  \
    KNOWN_OP(Greater)         \
    KNOWN_OP(GreaterEqual)    \
    KNOWN_OP(L2Normalization) \
    KNOWN_OP(Less)            \
    KNOWN_OP(LessEqual)       \
    KNOWN_OP(Logistic)        \
    KNOWN_OP(Lstm)            \
    KNOWN_OP(MaxPool2d)       \
    KNOWN_OP(Mean)            \
    KNOWN_OP(Mul)             \
    KNOWN_OP(Neg)             \
    KNOWN_OP(NotEqual)        \
    KNOWN_OP(Pad)             \
    KNOWN_OP(Relu)            \
    KNOWN_OP(Relu6)           \
    KNOWN_OP(ReluN1To1)       \
    KNOWN_OP(Reshape)         \
    KNOWN_OP(Shape)           \
    KNOWN_OP(Softmax)         \
    KNOWN_OP(SpaceToDepth)    \
    KNOWN_OP(Square)          \
    KNOWN_OP(Sub)             \
    KNOWN_OP(Tanh)

namespace hannk {
namespace {

constexpr char kDelegateName[] = "HannkDelegate";
constexpr int kDelegateVersion = 1;

// -------------------- Some glue code adapted from tfite/c/common.c
int TfLiteIntArrayGetSizeInBytes(int size) {
    static TfLiteIntArray dummy;
    return sizeof(dummy) + sizeof(dummy.data[0]) * size;
}

TfLiteIntArray *TfLiteIntArrayCreate(int size) {
    TfLiteIntArray *ret = (TfLiteIntArray *)malloc(TfLiteIntArrayGetSizeInBytes(size));
    ret->size = size;
    return ret;
}

void TfLiteIntArrayFree(TfLiteIntArray *a) {
    free(a);
}

struct TfLiteIntArrayDeleter {
    void operator()(TfLiteIntArray *a) {
        ::hannk::TfLiteIntArrayFree(a);
    }
};

std::unique_ptr<TfLiteIntArray, TfLiteIntArrayDeleter> BuildTfLiteIntArray(const std::vector<int> &data) {
    std::unique_ptr<TfLiteIntArray, TfLiteIntArrayDeleter> result(TfLiteIntArrayCreate(data.size()));
    std::copy(data.begin(), data.end(), result->data);
    return result;
}

// -------------------- Some glue code adapted from tfite/kernels/kernel_util.h

bool IsConstantTensor(const TfLiteTensor &tensor) {
    return tensor.allocation_type == kTfLiteMmapRo;
}

bool IsDynamicTensor(const TfLiteTensor &tensor) {
    return tensor.allocation_type == kTfLiteDynamic;
}

void SetTensorToDynamic(TfLiteContext *context, int tensor_id) {
    TfLiteTensor &tensor = context->tensors[tensor_id];
    if (tensor.allocation_type != kTfLiteDynamic) {
        tensor.allocation_type = kTfLiteDynamic;
        tensor.data.raw = nullptr;
    }
}

// -------------------- HannkDelegate

struct HannkDelegate final : public TfLiteDelegate {
    explicit HannkDelegate(const HannkDelegateOptions *p)
        : TfLiteDelegate(),
          options_(p != nullptr ? *p : HannkDelegateOptions()) {
        assert(this->data_ == nullptr);
        assert(this->CopyFromBufferHandle == nullptr);
        assert(this->CopyToBufferHandle == nullptr);
        assert(this->FreeBufferHandle == nullptr);
        this->Prepare = DelegatePrepare;
        this->flags = kTfLiteDelegateFlagsAllowDynamicTensors;
    }

    static TfLiteStatus DelegatePrepare(TfLiteContext *context, TfLiteDelegate *delegate);

    const HannkDelegateOptions options_;
};

// -------------------- HannkDelegateKernel

halide_type_t ConvertTfLiteType(TfLiteType t) {
    switch (t) {
    case kTfLiteBool:
        return halide_type_t(halide_type_uint, 1);
    case kTfLiteFloat16:
        return halide_type_t(halide_type_float, 16);
    case kTfLiteFloat32:
        return halide_type_t(halide_type_float, 32);
    case kTfLiteFloat64:
        return halide_type_t(halide_type_float, 64);
    case kTfLiteInt16:
        return halide_type_t(halide_type_int, 16);
    case kTfLiteInt32:
        return halide_type_t(halide_type_int, 32);
    case kTfLiteInt64:
        return halide_type_t(halide_type_int, 64);
    case kTfLiteInt8:
        return halide_type_t(halide_type_int, 8);
    case kTfLiteUInt8:
        return halide_type_t(halide_type_uint, 8);

    case kTfLiteString:
    case kTfLiteComplex64:
    case kTfLiteComplex128:
    default:
        CHECK(0) << "Unhandled type in ConvertTfLiteType";
        return halide_type_t();
    }
}

ActivationFunction ConvertTfLiteActivation(TfLiteFusedActivation a) {
    switch (a) {
    case kTfLiteActNone:
        return ActivationFunction::None;
    case kTfLiteActRelu:
        return ActivationFunction::Relu;
    case kTfLiteActReluN1To1:
        return ActivationFunction::ReluN1To1;
    case kTfLiteActRelu6:
        return ActivationFunction::Relu6;
    case kTfLiteActTanh:
        return ActivationFunction::Tanh;
    case kTfLiteActSignBit:
        return ActivationFunction::SignBit;
    case kTfLiteActSigmoid:
    default:
        CHECK(0) << "Unknown TfLiteFusedActivation";
    }
}

Padding ConvertTfLitePadding(TfLitePadding p) {
    switch (p) {
    case kTfLitePaddingSame:
        return Padding::Same;
    case kTfLitePaddingValid:
        return Padding::Valid;
    default:
        CHECK(0) << "Unknown TfLitePadding";
    }
}

std::vector<int> ConvertTfLiteShape(const TfLiteTensor &tensor) {
    assert(tensor.dims);
    const int shape_size = tensor.dims->size;
    std::vector<int> shape;
    shape.reserve(shape_size);
    for (int i = 0; i < shape_size; i++) {
        shape.push_back(tensor.dims->data[shape_size - 1 - i]);
    }
    return shape;
}

TensorPtr ConvertTfLiteTensor(const TfLiteTensor &tensor) {
    auto shape = ConvertTfLiteShape(tensor);

    halide_type_t type = ConvertTfLiteType(tensor.type);

    QuantizationInfo quantization;
    if (tensor.quantization.type == kTfLiteAffineQuantization) {
        const TfLiteAffineQuantization *q = (const TfLiteAffineQuantization *)tensor.quantization.params;
        for (int i = 0; i < q->scale->size; ++i) {
            const float scale = q->scale->data[i];
            quantization.scale.emplace_back(scale);
        }
        for (int i = 0; i < q->zero_point->size; i++) {
            const int zero = q->zero_point->data[i];
            quantization.zero.emplace_back(zero);
        }
        quantization.dimension = tensor.dims->size - q->quantized_dimension;
    }

    // tensor.name can be null, apparently. I don't think we have any requirement
    // for unique or non-empty names in our code, so let's just map that to
    // an empty string.
    const char *name = tensor.name ? tensor.name : "";

    if (IsConstantTensor(tensor)) {
        const void *read_only_data = (const uint8_t *)tensor.data.data;
        assert(read_only_data != nullptr);
        // Construct a HalideBuffer that points to read_only_data (but does not copy or own it).
        // Since TFLite will ensure that the TfLiteTensor remains valid while we're using it,
        // this should be completely safe.
        HalideBuffer<void> buffer(type, const_cast<void *>(read_only_data), shape);
        assert(tensor.bytes == buffer.size_in_bytes());

        return std::make_shared<Tensor>(name, std::move(buffer), std::move(quantization));
    }

    // Create an "unallocated" Buffer, which points to null.
    HalideBuffer<void> buffer(type, nullptr, shape);
    return std::make_shared<Tensor>(name, std::move(buffer), std::move(quantization));
}

class HannkDelegateKernel final {
public:
    // Each kernel instance will be used from only a single thread.
    // (It is fine for the kernel itself to use multiple threads internally.)
    explicit HannkDelegateKernel(const HannkDelegateOptions &options)
        : options_(options) {
    }

    // Init() will be called exactly once per instance.
    TfLiteStatus Init(TfLiteContext *context,
                      const TfLiteDelegateParams *params) {
        if (options_.verbosity >= 1) {
            LOG(INFO) << "Delegate " << (void *)this << " Init\n";
        }

        if (interpreter_ != nullptr) {
            TF_LITE_KERNEL_LOG(context, "Init must not be called twice.");
            return kTfLiteDelegateError;
        }

        std::vector<int> node_indices(params->nodes_to_replace->size);
        for (int i = 0; i < params->nodes_to_replace->size; i++) {
            const int node_index = params->nodes_to_replace->data[i];
            node_indices[i] = node_index;
        }
        if (options_.verbosity >= 1) {
            LOG(INFO) << "Delegate " << (void *)this << " Init nodes: " << node_indices << "\n";
        }

        // Pre-emptively map *all* the TFLiteTensors into our Tensor type.
        for (size_t tensor_id = 0; tensor_id < context->tensors_size; tensor_id++) {
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            if (tensor.dims == nullptr) {
                // Can't convert a TfLiteTensor with no dimension info
                continue;
            }
            auto t = ConvertTfLiteTensor(tensor);
            assert(!tensors_.count(tensor_id));
            tensors_[tensor_id] = t;
        }

        // Be careful with params->input_tensors and params->output_tensors here;
        // in particular, params->input_tensors will contain all of the 'constant'
        // input tensors (which are generally inputs only to a specific node).

        // Mark the input and output tensors correctly, as code in our interpreter
        // relies upon it.
        std::vector<TensorPtr> inputs;
        for (int i = 0; i < params->input_tensors->size; i++) {
            const int tensor_id = params->input_tensors->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            auto t = GetTensorById(context, tensor_id);
            t->set_input(true);
            inputs.push_back(t);
            if (options_.verbosity >= 2) {
                LOG(INFO) << "Delegate " << (void *)this << (t->is_constant() ? " Const" : "") << " Input tensor: " << tensor_id << "\n";
            }
        }

        // Add the output tensors.
        std::vector<TensorPtr> outputs;
        for (int i = 0; i < params->output_tensors->size; i++) {
            const int tensor_id = params->output_tensors->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            if (options_.verbosity >= 2) {
                LOG(INFO) << "Delegate " << (void *)this << " Output tensor: " << tensor_id << "\n";
            }
            auto t = GetTensorById(context, tensor_id);
            t->set_output(true);
            outputs.push_back(t);
        }

        // Add all ops.
        TfLiteNode *node;
        TfLiteRegistration *reg;
        std::vector<std::unique_ptr<Op>> ops;
        for (int node_index : node_indices) {
            TF_LITE_ENSURE_STATUS(context->GetNodeAndRegistration(context, node_index, &node, &reg));
            const int op_type = reg->builtin_code;
            std::unique_ptr<Op> op;

            // clang-format off
            switch (op_type) {
                #define KNOWN_OP(OP) case kTfLiteBuiltin##OP: op = Build##OP(context, node); break;
                ALL_KNOWN_OPS
                #undef KNOWN_OP

            default:
                TF_LITE_KERNEL_LOG(context, "Op not supported: %d", op_type);
                return kTfLiteDelegateError;
            }
            // clang-format on

            if (op == nullptr) {
                TF_LITE_KERNEL_LOG(context, "Op factory returned null: %s", op_type);
                return kTfLiteDelegateError;
            }
            ops.push_back(std::move(op));
        }
        model_ = ::hannk::make_unique<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));

        return kTfLiteOk;
    }

    // Prepare() will be called at least once, prior to any calls to Eval().
    // It will be called again if tensor shape(s) change. It is preferable
    // to do all memory allocation in Prepare(), rather than Eval(), if possible.
    TfLiteStatus Prepare(TfLiteContext *context, TfLiteNode *node) {
        if (options_.verbosity >= 1) {
            LOG(INFO) << "Delegate " << (void *)this << " Prepare\n";
        }

        assert(model_ != nullptr);

        if (interpreter_ != nullptr) {
            TF_LITE_KERNEL_LOG(context, "Calling Prepare() multiple times");
            return kTfLiteDelegateError;
        }

        interpreter_ = ::hannk::make_unique<Interpreter>(std::move(model_));

        for (int i = 0; i < node->outputs->size; i++) {
            const int tensor_id = node->outputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            auto t = GetTensorById(context, tensor_id);
            if (t && t->is_dynamic()) {
                if (options_.verbosity >= 2) {
                    LOG(INFO) << "SetTensorToDynamic " << tensor_id;
                }
                SetTensorToDynamic(context, tensor_id);
            }
        }

        return kTfLiteOk;
    }

    // Eval() will be called at least once. It can expect that Prepare() will
    // have been called for the current set of tensor shape(s).
    TfLiteStatus Eval(TfLiteContext *context, TfLiteNode *node) {
        if (options_.verbosity >= 1) {
            LOG(INFO) << "Delegate " << (void *)this << " Eval\n";
        }

        if (interpreter_ == nullptr) {
            TF_LITE_KERNEL_LOG(context, "interpreter_ is not built in Eval");
            return kTfLiteDelegateError;
        }

        // Copy the non-constant Tensor inputs. TODO: avoid this by sharing pointers.
        for (int i = 0; i < node->inputs->size; i++) {
            const int tensor_id = node->inputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            assert(tensor_id >= 0 && tensor_id < (int)context->tensors_size);
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            auto t = GetTensorById(context, tensor_id);
            assert(t->is_constant() == IsConstantTensor(tensor));
            if (t->is_constant()) {
                continue;
            }
            assert(t->is_input() && !t->is_constant() && t->is_allocated());
            auto buf = t->buffer();
            assert(buf.size_in_bytes() == tensor.bytes);
            memcpy(buf.data(), tensor.data.data, tensor.bytes);
        }

        // TODO: execute needs to return an error code.
        interpreter_->execute();

        // Copy the Tensor outputs. TODO: avoid this by sharing pointers.
        for (int i = 0; i < node->outputs->size; i++) {
            const int tensor_id = node->outputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            assert(tensor_id >= 0 && tensor_id < (int)context->tensors_size);
            TfLiteTensor &tensor = context->tensors[tensor_id];
            assert(!IsConstantTensor(tensor));
            auto t = GetTensorById(context, tensor_id);
            assert(t->is_output() && !t->is_constant() && t->is_allocated());
            if (t->is_dynamic()) {
                CHECK(IsDynamicTensor(tensor));
                const Box b = t->bounds();
                TfLiteIntArray *new_size = TfLiteIntArrayCreate((int)b.size());
                for (size_t i = 0; i < b.size(); i++) {
                    new_size->data[b.size() - i - 1] = b[i].extent();
                }
                // (Note that ResizeTensor takes ownership of new_size, even if an error is returned.)
                auto status = context->ResizeTensor(context, &tensor, new_size);
                if (status != kTfLiteOk) {
                    TF_LITE_KERNEL_LOG(context, "ResizeTensor() failed:", status);
                    return status;
                }
            }
            auto buf = t->buffer();
            assert(tensor.data.data != nullptr);
            assert(buf.data() != nullptr);
            assert(buf.size_in_bytes() == tensor.bytes);

            memcpy(tensor.data.data, buf.data(), tensor.bytes);
        }

        // Eval() could be called again with the same graph -- don't destroy the interpreter_ yet.

        return kTfLiteOk;
    }

    static TfLiteRegistration GetRegistration() {
        TfLiteRegistration r{};
        r.init = InitImpl;
        r.free = FreeImpl;
        r.prepare = PrepareImpl;
        r.invoke = InvokeImpl;
        r.profiling_string = nullptr;
        r.builtin_code = kTfLiteBuiltinDelegate;
        r.custom_name = kDelegateName;
        r.version = kDelegateVersion;
        return r;
    }

private:
    static void *InitImpl(TfLiteContext *context, const char *buffer, size_t length) {
        const TfLiteDelegateParams *params = (const TfLiteDelegateParams *)buffer;
        if (params == nullptr) {
            LOG(ERROR) << "HannkDelegate.init: NULL params";
            return nullptr;
        }
        HannkDelegate *hannk_delegate = (HannkDelegate *)params->delegate;
        auto self = ::hannk::make_unique<HannkDelegateKernel>(hannk_delegate->options_);
        if (self->Init(context, params) != kTfLiteOk) {
            LOG(ERROR) << "HannkDelegate.init: NULL params";
            return nullptr;
        }
        return self.release();
    };

    static void FreeImpl(TfLiteContext *context, void *buffer) {
        HannkDelegateKernel *self = (HannkDelegateKernel *)buffer;
        delete self;
    };

    static TfLiteStatus PrepareImpl(TfLiteContext *context, TfLiteNode *node) {
        if (node->user_data == nullptr) {
            LOG(ERROR) << "Delegate kernel was not initialized";
            return kTfLiteDelegateError;
        }
        HannkDelegateKernel *self = (HannkDelegateKernel *)node->user_data;
        return self->Prepare(context, node);
    };

    static TfLiteStatus InvokeImpl(TfLiteContext *context, TfLiteNode *node) {
        HannkDelegateKernel *self = (HannkDelegateKernel *)node->user_data;
        assert(self != nullptr);
        return self->Eval(context, node);
    };

    TensorPtr GetTensorById(TfLiteContext *context, int tensor_id) {
        auto it = tensors_.find(tensor_id);
        if (it == tensors_.end()) {
            LOG(ERROR) << "tensor_id not found: " << tensor_id;
            return nullptr;
        }
        return it->second;
    }

    template<typename OptionsT>
    std::unique_ptr<Op> BuildBinary(TfLiteContext *context, TfLiteNode *node, BinaryOp::Operator type) {
        auto input1 = GetTensorById(context, node->inputs->data[0]);
        auto input2 = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const OptionsT *params = (const OptionsT *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        return ::hannk::make_unique<BinaryOp>(input1, input2, output, type, activation);
    }

    std::unique_ptr<Op> BuildAdd(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary<TfLiteAddParams>(context, node, BinaryOp::Add);
    }

    std::unique_ptr<Op> BuildSub(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary<TfLiteSubParams>(context, node, BinaryOp::Sub);
    }

    std::unique_ptr<Op> BuildMul(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary<TfLiteMulParams>(context, node, BinaryOp::Mul);
    }

    std::unique_ptr<Op> BuildBinary(TfLiteContext *context, TfLiteNode *node, BinaryOp::Operator type, bool swap_operands = false) {
        auto input1 = GetTensorById(context, node->inputs->data[0]);
        auto input2 = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        if (swap_operands) {
            std::swap(input1, input2);
        }
        return ::hannk::make_unique<BinaryOp>(input1, input2, output, type);
    }

    std::unique_ptr<Op> BuildLess(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::Less);
    }

    std::unique_ptr<Op> BuildLessEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::LessEqual);
    }

    std::unique_ptr<Op> BuildGreater(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::Less, true);
    }

    std::unique_ptr<Op> BuildGreaterEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::LessEqual, true);
    }

    std::unique_ptr<Op> BuildEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::Equal);
    }

    std::unique_ptr<Op> BuildNotEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::NotEqual);
    }

    std::unique_ptr<Op> BuildPool2d(TfLiteContext *context, TfLiteNode *node, PoolOp::Operator reduce_op) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLitePoolParams *params = (const TfLitePoolParams *)(node->builtin_data);
        auto padding = ConvertTfLitePadding(params->padding);
        const std::vector<int> stride = {
            params->stride_width,
            params->stride_height,
        };
        const std::vector<int> filter_size = {
            params->filter_width,
            params->filter_height,
        };
        auto activation = ConvertTfLiteActivation(params->activation);
        return ::hannk::make_unique<PoolOp>(input, output, stride, filter_size, padding, reduce_op, activation);
    }

    std::unique_ptr<Op> BuildAveragePool2d(TfLiteContext *context, TfLiteNode *node) {
        return BuildPool2d(context, node, PoolOp::Average);
    }

    std::unique_ptr<Op> BuildMaxPool2d(TfLiteContext *context, TfLiteNode *node) {
        return BuildPool2d(context, node, PoolOp::Max);
    }

    std::unique_ptr<Op> BuildConcatenation(TfLiteContext *context, TfLiteNode *node) {
        std::vector<TensorPtr> inputs(node->inputs->size);
        for (int i = 0; i < node->inputs->size; i++) {
            inputs[i] = GetTensorById(context, node->inputs->data[i]);
        }
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteConcatenationParams *params = (const TfLiteConcatenationParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        CHECK(activation == ActivationFunction::None);
        int axis = params->axis;
        // Handle negative values, which are legal
        if (axis < 0) {
            axis = (int)output->rank() + axis;
        }
        // Now 'flip' the axis so that it refers to the right dimension in
        // the Tensor (since we reverse the dimension order)
        axis = (int)output->rank() - axis - 1;
        return ::hannk::make_unique<ConcatenationOp>(inputs, output, axis);
    }

    std::unique_ptr<Op> BuildConv2d(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteConvParams *params = (const TfLiteConvParams *)(node->builtin_data);
        auto padding = ConvertTfLitePadding(params->padding);
        const std::vector<int> stride = {
            params->stride_width,
            params->stride_height,
        };
        const std::vector<int> dilation_factor = {
            params->dilation_width_factor,
            params->dilation_height_factor,
        };
        auto activation = ConvertTfLiteActivation(params->activation);
        return ::hannk::make_unique<Conv2DOp>(input, filter, bias, output, stride,
                                              dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> BuildDepthwiseConv2d(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteDepthwiseConvParams *params = (const TfLiteDepthwiseConvParams *)(node->builtin_data);
        int depth_multiplier = output->extent(0) / input->extent(0);
        const std::vector<int> stride = {
            params->stride_width,
            params->stride_height,
        };
        const std::vector<int> dilation_factor = {
            params->dilation_width_factor,
            params->dilation_height_factor,
        };
        auto padding = ConvertTfLitePadding(params->padding);
        auto activation = ConvertTfLiteActivation(params->activation);
        return ::hannk::make_unique<DepthwiseConv2DOp>(input, filter, bias, output, depth_multiplier,
                                                       stride, dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> BuildFullyConnected(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteFullyConnectedParams *params = (const TfLiteFullyConnectedParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        return ::hannk::make_unique<FullyConnectedOp>(input, filter, bias, output, activation);
    }

    std::unique_ptr<Op> BuildPad(TfLiteContext *context, TfLiteNode *node) {
        // TODO: handle the PadOp that has 3 inputs
        CHECK(node->inputs->size == 2);
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto padding = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return ::hannk::make_unique<PadOp>(input, padding, output);
    }

    std::unique_ptr<Op> BuildReshape(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        TensorPtr shape_tensor = nullptr;
        std::vector<int> shape_array;
        if (node->inputs->size == 2) {
            shape_tensor = GetTensorById(context, node->inputs->data[1]);
        } else {
            const TfLiteReshapeParams *params = (const TfLiteReshapeParams *)(node->builtin_data);
            shape_array.assign(params->shape, params->shape + params->num_dimensions);
        }
        return ::hannk::make_unique<ReshapeOp>(input, shape_tensor, output, shape_array);
    }

    std::unique_ptr<Op> BuildShape(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return ::hannk::make_unique<ShapeOp>(input, output);
    }

    std::unique_ptr<Op> BuildSoftmax(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteSoftmaxParams *params = (const TfLiteSoftmaxParams *)(node->builtin_data);
        return ::hannk::make_unique<SoftmaxOp>(input, output, params->beta);
    }

    std::unique_ptr<Op> BuildL2Normalization(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return ::hannk::make_unique<L2NormalizationOp>(input, output);
    }

    std::unique_ptr<Op> BuildUnary(TfLiteContext *context, TfLiteNode *node, UnaryOp::Operator type) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return ::hannk::make_unique<UnaryOp>(input, output, type);
    }

    std::unique_ptr<Op> BuildLogistic(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Logistic);
    }

    std::unique_ptr<Op> BuildNeg(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Negate);
    }

    std::unique_ptr<Op> BuildTanh(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Tanh);
    }

    std::unique_ptr<Op> BuildRelu(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Relu);
    }

    std::unique_ptr<Op> BuildRelu6(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Relu6);
    }

    std::unique_ptr<Op> BuildReluN1To1(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::ReluN1To1);
    }

    std::unique_ptr<Op> BuildMean(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto indices = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return ::hannk::make_unique<ReductionOp>(input, indices, output, ReductionOp::Mean);
    }

    std::unique_ptr<Op> BuildSpaceToDepth(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteSpaceToDepthParams *params = (const TfLiteSpaceToDepthParams *)(node->builtin_data);
        return ::hannk::make_unique<SpaceDepthOp>(input, output, params->block_size);
    }

    std::unique_ptr<Op> BuildSquare(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Square);
    }

    std::unique_ptr<Op> BuildDepthToSpace(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteDepthToSpaceParams *params = (const TfLiteDepthToSpaceParams *)(node->builtin_data);
        return ::hannk::make_unique<SpaceDepthOp>(input, output, -params->block_size);
    }

    std::unique_ptr<Op> BuildLstm(TfLiteContext *context, TfLiteNode *node) {
        // Note that the TFLite 'Lstm' op is lowered into several Hannk ops
        auto data_input = GetTensorById(context, node->inputs->data[0]);
        auto prev_activ_input = GetTensorById(context, node->inputs->data[1]);
        auto weights_input = GetTensorById(context, node->inputs->data[2]);
        auto biases_input = GetTensorById(context, node->inputs->data[3]);
        auto prev_state_input = GetTensorById(context, node->inputs->data[4]);

        auto activ_output = GetTensorById(context, node->outputs->data[0]);
        auto state_output = GetTensorById(context, node->outputs->data[1]);
        auto concat_temp = GetTensorById(context, node->outputs->data[2]);
        auto activ_temp = GetTensorById(context, node->outputs->data[3]);

        return lower_tflite_lstm(data_input, prev_activ_input, weights_input, biases_input, prev_state_input,
                                 activ_output, state_output, concat_temp, activ_temp);
    }

    const HannkDelegateOptions options_;
    std::unique_ptr<OpGroup> model_;
    std::unique_ptr<Interpreter> interpreter_;
    // TODO: unordered_map might be a better choice.
    std::map<int, TensorPtr> tensors_;
};

class NodeSupport {
    TfLiteContext *context;
    TfLiteNode *node;
    TfLiteRegistration *registration;

    // static const[expr] is a better choice but requires C++17 to avoid pain.
    enum PossibleTypesMask {
        NONE = 1 << kTfLiteNoType,
        U8 = 1 << kTfLiteUInt8,
        I8 = 1 << kTfLiteInt8,
        I16 = 1 << kTfLiteInt16,
        I32 = 1 << kTfLiteInt32,
        F32 = 1 << kTfLiteFloat32,
        F64 = 1 << kTfLiteFloat64,
        I32_OR_NONE = I32 | NONE,
        ANY_ARITHMETIC = U8 | I8 | I16 | I32 | F32 | F64
    };

    bool InputsHaveCorrectTypes(std::initializer_list<PossibleTypesMask> per_input_possible_types_mask) const {
        if (node->inputs->size != (int)per_input_possible_types_mask.size()) {
            LOG(ERROR) << "inputs size mismatch in InputsHaveCorrectTypes";
            return false;
        }
        int i = -1;
        for (PossibleTypesMask possible_types_mask : per_input_possible_types_mask) {
            ++i;
            // Skip optional tensor.
            const int tensor_id = node->inputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            const int tensor_type_mask = 1 << tensor.type;
            if (!(tensor_type_mask & possible_types_mask)) {
                return false;
            }
        }
        return true;
    }

    bool AllInputsHaveType(PossibleTypesMask possible_types_mask) const {
        for (int i = 0; i < node->inputs->size; ++i) {
            const int tensor_id = node->inputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            const int tensor_type_mask = 1 << tensor.type;
            if (!(tensor_type_mask & possible_types_mask)) {
                return false;
            }
        }
        return true;
    }

    bool TensorsHaveCorrectTypes(std::initializer_list<std::pair<int, PossibleTypesMask>> tensor_ids_and_masks) const {
        for (const auto &it : tensor_ids_and_masks) {
            const int tensor_id = it.first;
            const PossibleTypesMask possible_types_mask = it.second;
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            const int tensor_type_mask = 1 << tensor.type;
            if (!(tensor_type_mask & possible_types_mask)) {
                return false;
            }
        }
        return true;
    }

    bool IsActivationReluOrNone(TfLiteFusedActivation activation) const {
        return (activation == kTfLiteActRelu ||
                activation == kTfLiteActRelu6 ||
                activation == kTfLiteActReluN1To1 ||
                activation == kTfLiteActNone);
    }

    bool IsNodeSupported_Add() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8})) {
            return false;
        }
        const TfLiteAddParams *params = (const TfLiteAddParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Sub() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8})) {
            return false;
        }
        const TfLiteSubParams *params = (const TfLiteSubParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Mul() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8})) {
            return false;
        }
        const TfLiteMulParams *params = (const TfLiteMulParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Compare() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY_ARITHMETIC, ANY_ARITHMETIC})) {
            return false;
        }
        if (node->outputs->size != 1) {
            return false;
        }
        const TfLiteTensor &output = context->tensors[node->outputs->data[0]];
        if (output.type != kTfLiteBool || output.dims->size != 0) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Less() const {
        return IsNodeSupported_Compare();
    }

    bool IsNodeSupported_LessEqual() const {
        return IsNodeSupported_Compare();
    }

    bool IsNodeSupported_Greater() const {
        return IsNodeSupported_Compare();
    }

    bool IsNodeSupported_GreaterEqual() const {
        return IsNodeSupported_Compare();
    }

    bool IsNodeSupported_Equal() const {
        return IsNodeSupported_Compare();
    }

    bool IsNodeSupported_NotEqual() const {
        return IsNodeSupported_Compare();
    }

    bool IsNodeSupported_Concatenation() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!AllInputsHaveType(U8)) {
            return false;
        }
        // TODO: This op has an activation but we don't appear to use it.
        // const TfLiteConcatenationParams *params = (const TfLiteConcatenationParams *)(node->builtin_data);
        return true;
    }

    bool IsNodeSupported_Conv2d() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8, I32})) {
            return false;
        }
        const TfLiteConvParams *params = (const TfLiteConvParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_DepthwiseConv2d() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8, I32})) {
            return false;
        }
        const TfLiteDepthwiseConvParams *params = (const TfLiteDepthwiseConvParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_FullyConnected() const {
        // This is correct, we don't handle the params for v2 or later yet
        if (!(registration->version <= 1)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8, I32_OR_NONE})) {
            return false;
        }
        const TfLiteFullyConnectedParams *params = (const TfLiteFullyConnectedParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Pool2d() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        const TfLitePoolParams *params = (const TfLitePoolParams *)(node->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_AveragePool2d() const {
        return IsNodeSupported_Pool2d();
    }

    bool IsNodeSupported_MaxPool2d() const {
        return IsNodeSupported_Pool2d();
    }

    bool IsNodeSupported_Pad() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, I32})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Reshape() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        // Note that Reshape can have 1 or 2 inputs.
        if (node->inputs->size > 2) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Shape() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (node->inputs->size != 1) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Softmax() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_L2Normalization() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Unary() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Logistic() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_Neg() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_Tanh() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_Relu() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_Relu6() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_ReluN1To1() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_Square() const {
        return IsNodeSupported_Unary();
    }

    bool IsNodeSupported_Mean() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, I32})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_SpaceToDepth() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_DepthToSpace() const {
        if (!(registration->version <= 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Lstm() const {
        // TODO: we might work with v3 or v4, but haven't tested any instances.
        if (!(registration->version <= 2)) {
            return false;
        }

        if (node->inputs->size != 5 || node->outputs->size != 4) {
            return false;
        }

        // Our 'Lstm' op is actually a group of several Hannk ops;
        // we must check these carefully (see lower_tflite_lstm() for reference):

        const int data_input = node->inputs->data[0];
        const int prev_activ_input = node->inputs->data[1];
        const int weights_input = node->inputs->data[2];
        const int biases_input = node->inputs->data[3];
        const int prev_state_input = node->inputs->data[4];

        const int activ_output = node->outputs->data[0];
        const int state_output = node->outputs->data[1];
        const int concat_temp = node->outputs->data[2];
        const int activ_temp = node->outputs->data[3];

        if (!TensorsHaveCorrectTypes({
                {data_input, U8},
                {prev_activ_input, U8},
                {weights_input, U8},
                {biases_input, I32},
                {prev_state_input, I16},
                {activ_output, U8},
                {state_output, I16},
                {concat_temp, U8},
                {activ_temp, I16},
            })) {
            return false;
        }

        const TfLiteLSTMParams *params = (const TfLiteLSTMParams *)(node->builtin_data);
        // TODO: there is an activation function specified here but it's not clear
        // whether it's used in the LSTM reference implementation. Ignoring for now.
        // if (params->activation == ...) {
        //     return false;
        // }

        // TODO: for v2+, you can specify 'basic' vs 'full' kernels.
        // The 'basic' kernel is all we've tested with.
        if (registration->version >= 2) {
            if (params->kernel_type != kTfLiteLSTMBasicKernel) {
                return false;
            }
        }

        return true;
    }

public:
    NodeSupport(TfLiteContext *c,
                TfLiteNode *n,
                TfLiteRegistration *r)
        : context(c), node(n), registration(r) {
    }

    bool IsNodeSupported() const {
        // Ensure all inputs & outputs have dim <= 4.
        for (int i = 0; i < node->inputs->size; ++i) {
            const int tensor_id = node->inputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            assert(tensor.dims);
            if (tensor.dims->size > 4) {
                return false;
            }
        }
        for (int i = 0; i < node->outputs->size; ++i) {
            const int tensor_id = node->outputs->data[i];
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            assert(tensor.dims);
            if (tensor.dims->size > 4) {
                return false;
            }
        }

        // Now check for each specific node.
        //
        // TODO: Our existing code for TFLiteParser, etc doesn't pay
        // attention to version (AFAICT); need to find & examine the specs of
        // version changes to ensure this is correct. Existing version checking
        // here is mostly bogus. See tensorflow/lite/tools/versioning/op_version.cc
        //
        // TODO: style here is imitation of approach used in Hexagon delegate,
        // but a purely data-table-driven-approach might be better in the long run?

        // clang-format off
        switch (registration->builtin_code) {

            #define KNOWN_OP(OP) case kTfLiteBuiltin##OP: return IsNodeSupported_##OP();
            ALL_KNOWN_OPS
            #undef KNOWN_OP

        default:
            return false;
        }
        // clang-format on

        return false;
    }
};
/*static*/ TfLiteStatus HannkDelegate::DelegatePrepare(TfLiteContext *context, TfLiteDelegate *delegate) {
    HannkDelegate *self = (HannkDelegate *)delegate;

    TfLiteStatus status;

    TfLiteIntArray *plan = nullptr;
    if ((status = context->GetExecutionPlan(context, &plan)) != kTfLiteOk) {
        LOG(ERROR) << "GetExecutionPlan failed";
        return status;
    }

    // Build up a list of the nodes we want to handle.
    std::vector<int> supported_nodes;
    for (int i = 0; i < plan->size; i++) {
        const int node_index = plan->data[i];
        TfLiteNode *node;
        TfLiteRegistration *registration;
        if ((status = context->GetNodeAndRegistration(context, node_index, &node, &registration)) != kTfLiteOk) {
            LOG(ERROR) << "GetNodeAndRegistration failed";
            return status;
        }

        NodeSupport support(context, node, registration);
        if (support.IsNodeSupported()) {
            if (self->options_.verbosity >= 1) {
                LOG(INFO) << "Handling node, index=" << node_index << " code=" << registration->builtin_code;
            }
            supported_nodes.push_back(node_index);
        } else {
            if (self->options_.verbosity >= 1) {
                // NOTE: The TFLite C API doesn't provide a way to map builtin_code
                // to a readable name; see lite/builtin_ops.h to find what sort
                // of node(s) we are skipping here. (The names are available if
                // we add a dependency on the generated schema file, but that's a
                // dep we don't otherwise need or want here.)
                LOG(INFO) << "Skipping unsupported node, index=" << node_index
                          << " code=" << registration->builtin_code
                          << " version=" << registration->version
                          << " custom_name=(" << (registration->custom_name ? registration->custom_name : "nullptr") << ")"
                          << "\n";
            }
        }
    }

    if ((status = context->ReplaceNodeSubsetsWithDelegateKernels(context,
                                                                 HannkDelegateKernel::GetRegistration(),
                                                                 BuildTfLiteIntArray(supported_nodes).get(),
                                                                 delegate)) != kTfLiteOk) {
        LOG(ERROR) << "ReplaceNodeSubsetsWithDelegateKernels failed";
        return status;
    }

    return kTfLiteOk;
}

}  // namespace
}  // namespace hannk

TfLiteDelegate *HannkDelegateCreate(const HannkDelegateOptions *options) {
    using hannk::HannkDelegate;

    return new HannkDelegate(options);
}

void HannkDelegateOptionsDefault(HannkDelegateOptions *opt) {
    *opt = HannkDelegateOptions();
}

void HannkDelegateDelete(TfLiteDelegate *delegate) {
    using hannk::HannkDelegate;

    if (delegate) {
        HannkDelegate *hannk_delegate = (HannkDelegate *)delegate;
        delete hannk_delegate;
    }
}
