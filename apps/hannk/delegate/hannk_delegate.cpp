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
#include "tensorflow/lite/context_util.h"
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
    KNOWN_OP(Gather)          \
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
    KNOWN_OP(Split)           \
    KNOWN_OP(SplitV)          \
    KNOWN_OP(Square)          \
    KNOWN_OP(Sub)             \
    KNOWN_OP(Tanh)            \
    KNOWN_OP(Transpose)

namespace hannk {
namespace {

using tflite::TfLiteIntArrayView;

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

#ifndef NDEBUG
bool IsDynamicTensor(const TfLiteTensor &tensor) {
    return tensor.allocation_type == kTfLiteDynamic;
}
#endif

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
        HCHECK(0) << "Unhandled type in ConvertTfLiteType";
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
        HCHECK(0) << "Unknown TfLiteFusedActivation";
    }
}

Padding ConvertTfLitePadding(TfLitePadding p) {
    switch (p) {
    case kTfLitePaddingSame:
        return Padding::Same;
    case kTfLitePaddingValid:
        return Padding::Valid;
    default:
        HCHECK(0) << "Unknown TfLitePadding";
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

        auto p = std::make_shared<Tensor>(name, std::move(buffer), std::move(quantization));
        p->set_constant();
        return p;
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
    TfLiteStatus Init(TfLiteContext *context, const TfLiteDelegateParams *params) {
        if (options_.verbosity >= 1) {
            HLOG(INFO) << "Delegate " << (void *)this << " Init\n";
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
            HLOG(INFO) << "Delegate " << (void *)this << " Init nodes: " << node_indices << "\n";
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
            inputs.push_back(t);
            if (options_.verbosity >= 2) {
                HLOG(INFO) << "Delegate " << (void *)this << (t->is_constant() ? " Const" : "") << " Input tensor: " << tensor_id << "\n";
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
                HLOG(INFO) << "Delegate " << (void *)this << " Output tensor: " << tensor_id << "\n";
            }
            auto t = GetTensorById(context, tensor_id);
            outputs.push_back(t);
        }

        // Add all ops.
        TfLiteNode *node;
        TfLiteRegistration *reg;
        std::vector<OpPtr> ops;
        for (int node_index : node_indices) {
            TF_LITE_ENSURE_STATUS(context->GetNodeAndRegistration(context, node_index, &node, &reg));
            const int op_type = reg->builtin_code;
            OpPtr op;

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
        model_ = make_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));

        return kTfLiteOk;
    }

    // Prepare() will be called at least once, prior to any calls to Eval().
    // It will be called again if tensor shape(s) change. It is preferable
    // to do all memory allocation in Prepare(), rather than Eval(), if possible.
    TfLiteStatus Prepare(TfLiteContext *context, TfLiteNode *node) {
        if (options_.verbosity >= 1) {
            HLOG(INFO) << "Delegate " << (void *)this << " Prepare\n";
        }

        assert(model_ != nullptr);

        if (interpreter_ != nullptr) {
            TF_LITE_KERNEL_LOG(context, "Calling Prepare() multiple times");
            return kTfLiteDelegateError;
        }

        // All inputs and outputs that aren't dynamic are marked as 'external',
        // so that we can share memory between TFLite and Hannk. (Note that the
        // TFLite Tensors haven't been allocated yet; we must update the host
        // pointers in Eval.)
        const auto set_external = [this, context](int tensor_id) {
            assert(tensor_id != kTfLiteOptionalTensor);
            auto t = GetTensorById(context, tensor_id);
            if (!t->is_dynamic()) {
                t->set_external();
            }
        };

        // Mark all inputs and outputs as 'external'; we'll rely on TFLite
        // to allocate the memory for these, and our internal hannk Tensors
        // will shadow that memory, to save both space & copying time.
        for (int tensor_id : TfLiteIntArrayView(node->inputs)) {
            set_external(tensor_id);
        }
        for (int tensor_id : TfLiteIntArrayView(node->outputs)) {
            set_external(tensor_id);
        }

        InterpreterOptions options;
        options.verbosity = options_.verbosity;
        interpreter_ = std::unique_ptr<Interpreter>(new Interpreter(std::move(model_), std::move(options)));
        if (!interpreter_->prepare()) {
            TF_LITE_KERNEL_LOG(context, "hannk::Interpreter::prepare() failed");
            return kTfLiteDelegateError;
        }

        for (int tensor_id : TfLiteIntArrayView(node->outputs)) {
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            auto t = GetTensorById(context, tensor_id);
            if (t && t->is_dynamic()) {
                assert(!t->is_external());
                if (options_.verbosity >= 2) {
                    HLOG(INFO) << "SetTensorToDynamic " << tensor_id;
                }
                SetTensorToDynamic(context, tensor_id);
            }
        }

        return kTfLiteOk;
    }

    // Eval() will be called at least once. It can expect that Prepare() will
    // have been called for the current set of tensor shape(s).
    TfLiteStatus Eval(TfLiteContext *context, TfLiteNode *node) {
        if (options_.verbosity >= 3) {
            HLOG(INFO) << "Delegate " << (void *)this << " Eval\n";
        }

        if (interpreter_ == nullptr) {
            TF_LITE_KERNEL_LOG(context, "interpreter_ is not built in Eval");
            return kTfLiteDelegateError;
        }

        const auto set_host = [this, context](int tensor_id) {
            assert(tensor_id != kTfLiteOptionalTensor);
            auto t = GetTensorById(context, tensor_id);
            if (t->is_external()) {
                assert(!t->is_dynamic());
                TfLiteTensor &tensor = context->tensors[tensor_id];
                // TODO: should this be upgraded to a runtime-check-and-return-error?
                const auto &old_buf = t->buffer();
                assert(old_buf.size_in_bytes() == tensor.bytes);
                // We must reset it every time, as the tensor's data pointer
                // can vary between calls in some scenatrios.
                const auto *raw_buf = old_buf.raw_buffer();
                HalideBuffer<void> buf(raw_buf->type, tensor.data.data, raw_buf->dimensions, raw_buf->dim);
                t->set_external_buffer(std::move(buf));
            }
        };

        for (int tensor_id : TfLiteIntArrayView(node->inputs)) {
            set_host(tensor_id);
        }
        for (int tensor_id : TfLiteIntArrayView(node->outputs)) {
            set_host(tensor_id);
        }

        // TODO: execute needs to return an error code.
        interpreter_->execute();

        // Dynamic tensors can't share their memory, because we didn't
        // necessarily know the size until the pipeline was executed,
        // so we need to resize the TFLite tensor and copy the data
        // back over. This is regrettable, but dynamic tensors tend to
        // to be uncommon.
        for (int tensor_id : TfLiteIntArrayView(node->outputs)) {
            assert(tensor_id != kTfLiteOptionalTensor);
            auto t = GetTensorById(context, tensor_id);
            if (t->is_dynamic()) {
                TfLiteTensor &tensor = context->tensors[tensor_id];
                assert(IsDynamicTensor(tensor));
                const Box b = t->bounds();
                if (options_.verbosity >= 2) {
                    HLOG(INFO) << "ResizeTensor " << tensor_id << " to " << b;
                }
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
                auto buf = t->buffer();
                assert(tensor.data.data != nullptr);
                assert(buf.data() != nullptr);
                assert(buf.size_in_bytes() == tensor.bytes);

                memcpy(tensor.data.data, buf.data(), tensor.bytes);
            }
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
            HLOG(ERROR) << "HannkDelegate.init: NULL params";
            return nullptr;
        }
        HannkDelegate *hannk_delegate = (HannkDelegate *)params->delegate;
        std::unique_ptr<HannkDelegateKernel> self(new HannkDelegateKernel(hannk_delegate->options_));
        if (self->Init(context, params) != kTfLiteOk) {
            HLOG(ERROR) << "HannkDelegate.init: NULL params";
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
            HLOG(ERROR) << "Delegate kernel was not initialized";
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
            HLOG(ERROR) << "tensor_id not found: " << tensor_id;
            return nullptr;
        }
        return it->second;
    }

    template<typename OptionsT>
    OpPtr BuildBinary(TfLiteContext *context, TfLiteNode *node, BinaryOp::Operator type) {
        auto input1 = GetTensorById(context, node->inputs->data[0]);
        auto input2 = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const OptionsT *params = (const OptionsT *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        return make_op<BinaryOp>(input1, input2, output, type, activation);
    }

    OpPtr BuildAdd(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary<TfLiteAddParams>(context, node, BinaryOp::Add);
    }

    OpPtr BuildSub(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary<TfLiteSubParams>(context, node, BinaryOp::Sub);
    }

    OpPtr BuildMul(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary<TfLiteMulParams>(context, node, BinaryOp::Mul);
    }

    OpPtr BuildBinary(TfLiteContext *context, TfLiteNode *node, BinaryOp::Operator type, bool swap_operands = false) {
        auto input1 = GetTensorById(context, node->inputs->data[0]);
        auto input2 = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        if (swap_operands) {
            std::swap(input1, input2);
        }
        return make_op<BinaryOp>(input1, input2, output, type);
    }

    OpPtr BuildLess(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::Less);
    }

    OpPtr BuildLessEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::LessEqual);
    }

    OpPtr BuildGreater(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::LessEqual, true);
    }

    OpPtr BuildGreaterEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::Less, true);
    }

    OpPtr BuildEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::Equal);
    }

    OpPtr BuildNotEqual(TfLiteContext *context, TfLiteNode *node) {
        return BuildBinary(context, node, BinaryOp::NotEqual);
    }

    OpPtr BuildPool2d(TfLiteContext *context, TfLiteNode *node, Pool2DOp::Operator reduce_op) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLitePoolParams *params = (const TfLitePoolParams *)(node->builtin_data);
        auto padding = ConvertTfLitePadding(params->padding);
        const std::array<int, 2> stride = {{
            params->stride_width,
            params->stride_height,
        }};
        const std::array<int, 2> filter_size = {{
            params->filter_width,
            params->filter_height,
        }};
        auto activation = ConvertTfLiteActivation(params->activation);
        return make_op<Pool2DOp>(input, output, stride, filter_size, padding, reduce_op, activation);
    }

    OpPtr BuildAveragePool2d(TfLiteContext *context, TfLiteNode *node) {
        return BuildPool2d(context, node, Pool2DOp::Average);
    }

    OpPtr BuildMaxPool2d(TfLiteContext *context, TfLiteNode *node) {
        return BuildPool2d(context, node, Pool2DOp::Max);
    }

    OpPtr BuildConcatenation(TfLiteContext *context, TfLiteNode *node) {
        std::vector<TensorPtr> inputs(node->inputs->size);
        for (int i = 0; i < node->inputs->size; i++) {
            inputs[i] = GetTensorById(context, node->inputs->data[i]);
        }
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteConcatenationParams *params = (const TfLiteConcatenationParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        HCHECK(activation == ActivationFunction::None);
        int axis = params->axis;
        // Handle negative values, which are legal
        if (axis < 0) {
            axis = (int)output->rank() + axis;
        }
        // Now 'flip' the axis so that it refers to the right dimension in
        // the Tensor (since we reverse the dimension order)
        axis = (int)output->rank() - axis - 1;
        return make_op<ConcatenationOp>(inputs, output, axis);
    }

    OpPtr BuildConv2d(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteConvParams *params = (const TfLiteConvParams *)(node->builtin_data);
        auto padding = ConvertTfLitePadding(params->padding);
        const std::array<int, 2> stride = {{
            params->stride_width,
            params->stride_height,
        }};
        const std::array<int, 2> dilation_factor = {{
            params->dilation_width_factor,
            params->dilation_height_factor,
        }};
        auto activation = ConvertTfLiteActivation(params->activation);
        return make_op<ConvOp>(input, filter, bias, output, stride,
                               dilation_factor, padding, activation);
    }

    OpPtr BuildDepthwiseConv2d(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteDepthwiseConvParams *params = (const TfLiteDepthwiseConvParams *)(node->builtin_data);
        int depth_multiplier = output->extent(0) / input->extent(0);
        const std::array<int, 2> stride = {{
            params->stride_width,
            params->stride_height,
        }};
        const std::array<int, 2> dilation_factor = {{
            params->dilation_width_factor,
            params->dilation_height_factor,
        }};
        auto padding = ConvertTfLitePadding(params->padding);
        auto activation = ConvertTfLiteActivation(params->activation);
        return make_op<DepthwiseConv2DOp>(input, filter, bias, output, depth_multiplier,
                                          stride, dilation_factor, padding, activation);
    }

    OpPtr BuildFullyConnected(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteFullyConnectedParams *params = (const TfLiteFullyConnectedParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        return lower_tflite_fullyconnected(input, filter, bias, output, activation);
    }

    OpPtr BuildPad(TfLiteContext *context, TfLiteNode *node) {
        // TODO: handle the PadOp that has 3 inputs
        HCHECK(node->inputs->size == 2);
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto padding = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return make_op<PadOp>(input, padding, output);
    }

    OpPtr BuildGather(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto indices = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteGatherParams *params = (const TfLiteGatherParams *)(node->builtin_data);
        int axis = params->axis;
        int batch_dims = params->batch_dims;
        if (axis < 0) {
            axis += input->rank();
        }
        axis = input->rank() - 1 - axis;
        return make_op<GatherOp>(input, indices, output, axis, batch_dims);
    }

    // TODO: support GATHER_ND once we find a testcase for it
    // OpPtr BuildGatherNd(TfLiteContext *context, TfLiteNode *node) {
    //     return BuildGather(context, node);
    // }

    OpPtr BuildReshape(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        TensorPtr shape_tensor = nullptr;
        if (node->inputs->size == 2) {
            shape_tensor = GetTensorById(context, node->inputs->data[1]);
        } else {
            const TfLiteReshapeParams *params = (const TfLiteReshapeParams *)(node->builtin_data);
            if (params) {
                HalideBuffer<int32_t> shape_data(const_cast<int32_t *>(params->shape), params->num_dimensions);
                shape_tensor = std::make_shared<Tensor>(input->name() + "_shape", shape_data);
                shape_tensor->set_constant();
            }
        }
        return make_op<ReshapeOp>(input, shape_tensor, output);
    }

    OpPtr BuildShape(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return make_op<ShapeOp>(input, output);
    }

    OpPtr BuildSoftmax(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteSoftmaxParams *params = (const TfLiteSoftmaxParams *)(node->builtin_data);
        const int axis = 0;  // In TFLite, normalization is always against the first axis.
        return make_op<SoftmaxOp>(input, output, params->beta, axis);
    }

    OpPtr BuildL2Normalization(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const int axis = 0;  // In TFLite, normalization is always against the first axis.
        return make_op<L2NormalizationOp>(input, output, axis);
    }

    OpPtr BuildUnary(TfLiteContext *context, TfLiteNode *node, UnaryOp::Operator type) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return make_op<UnaryOp>(input, output, type);
    }

    OpPtr BuildLogistic(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Logistic);
    }

    OpPtr BuildNeg(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Negate);
    }

    OpPtr BuildTanh(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Tanh);
    }

    OpPtr BuildRelu(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Relu);
    }

    OpPtr BuildRelu6(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Relu6);
    }

    OpPtr BuildReluN1To1(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::ReluN1To1);
    }

    OpPtr BuildMean(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto indices = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteReducerParams *params = (const TfLiteReducerParams *)(node->builtin_data);
#ifndef NDEBUG
        const bool keep_dims = params ? params->keep_dims : false;
        // TODO: I have yet to find any examples of keep_dims == false in the wild.
        // If/when we do, handle it appropriately.
        assert(keep_dims == true);
#endif
        return make_op<ReductionOp>(ReductionOp::Mean, input, indices, output);
    }

    OpPtr BuildSpaceToDepth(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteSpaceToDepthParams *params = (const TfLiteSpaceToDepthParams *)(node->builtin_data);
        return make_op<SpaceDepthOp>(input, output, params->block_size);
    }

    OpPtr BuildSplit(TfLiteContext *context, TfLiteNode *node, int axis_tensor_index, int input_tensor_index) {
        assert(axis_tensor_index < node->inputs->size);
        auto axis_tensor = GetTensorById(context, node->inputs->data[axis_tensor_index]);
        HCHECK(axis_tensor->is_allocated()) << "Can't handle dynamic axis for Split.\n";
        int axis = axis_tensor->buffer<int32_t>()();

        assert(input_tensor_index < node->inputs->size);
        auto input = GetTensorById(context, node->inputs->data[input_tensor_index]);
        std::vector<TensorPtr> outputs(node->outputs->size);
        for (int i = 0; i < node->outputs->size; i++) {
            outputs[i] = GetTensorById(context, node->outputs->data[i]);
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

    OpPtr BuildSplit(TfLiteContext *context, TfLiteNode *node) {
        return BuildSplit(context, node, 0, 1);
    }

    OpPtr BuildSplitV(TfLiteContext *context, TfLiteNode *node) {
        return BuildSplit(context, node, 2, 0);
    }

    OpPtr BuildSquare(TfLiteContext *context, TfLiteNode *node) {
        return BuildUnary(context, node, UnaryOp::Square);
    }

    OpPtr BuildDepthToSpace(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteDepthToSpaceParams *params = (const TfLiteDepthToSpaceParams *)(node->builtin_data);
        return make_op<SpaceDepthOp>(input, output, -params->block_size);
    }

    OpPtr BuildLstm(TfLiteContext *context, TfLiteNode *node) {
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

    OpPtr BuildTranspose(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto dims = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return make_op<TransposeOp>(input, dims, output);
    }

    const HannkDelegateOptions options_;
    std::unique_ptr<OpGroup> model_;
    std::unique_ptr<Interpreter> interpreter_;
    // TODO: unordered_map might be a better choice.
    std::map<int, TensorPtr> tensors_;
};

const char *GetOpName(int op) {
    // As of TFLite 2.4
    constexpr int count = 129;
    static const char *const names[count] = {
        "ADD",
        "AVERAGEPOOL2D",
        "CONCATENATION",
        "CONV2D",
        "DEPTHWISECONV2D",
        "DEPTHTOSPACE",
        "DEQUANTIZE",
        "EMBEDDINGLOOKUP",
        "FLOOR",
        "FULLYCONNECTED",
        "HASHTABLELOOKUP",
        "L2NORMALIZATION",
        "L2POOL2D",
        "LOCALRESPONSENORMALIZATION",
        "LOGISTIC",
        "LSHPROJECTION",
        "LSTM",
        "MAXPOOL2D",
        "MUL",
        "RELU",
        "RELUN1TO1",
        "RELU6",
        "RESHAPE",
        "RESIZEBILINEAR",
        "RNN",
        "SOFTMAX",
        "SPACETODEPTH",
        "SVDF",
        "TANH",
        "CONCATEMBEDDINGS",
        "SKIPGRAM",
        "CALL",
        "CUSTOM",
        "EMBEDDINGLOOKUPSPARSE",
        "PAD",
        "UNIDIRECTIONALSEQUENCERNN",
        "GATHER",
        "BATCHTOSPACEND",
        "SPACETOBATCHND",
        "TRANSPOSE",
        "MEAN",
        "SUB",
        "DIV",
        "SQUEEZE",
        "UNIDIRECTIONALSEQUENCELSTM",
        "STRIDEDSLICE",
        "BIDIRECTIONALSEQUENCERNN",
        "EXP",
        "TOPKV2",
        "SPLIT",
        "LOGSOFTMAX",
        "DELEGATE",
        "BIDIRECTIONALSEQUENCELSTM",
        "CAST",
        "PRELU",
        "MAXIMUM",
        "ARGMAX",
        "MINIMUM",
        "LESS",
        "NEG",
        "PADV2",
        "GREATER",
        "GREATEREQUAL",
        "LESSEQUAL",
        "SELECT",
        "SLICE",
        "SIN",
        "TRANSPOSECONV",
        "SPARSETODENSE",
        "TILE",
        "EXPANDDIMS",
        "EQUAL",
        "NOTEQUAL",
        "HLOG",
        "SUM",
        "SQRT",
        "RSQRT",
        "SHAPE",
        "POW",
        "ARGMIN",
        "FAKEQUANT",
        "REDUCEPROD",
        "REDUCEMAX",
        "PACK",
        "LOGICALOR",
        "ONEHOT",
        "LOGICALAND",
        "LOGICALNOT",
        "UNPACK",
        "REDUCEMIN",
        "FLOORDIV",
        "REDUCEANY",
        "SQUARE",
        "ZEROSLIKE",
        "FILL",
        "FLOORMOD",
        "RANGE",
        "RESIZENEARESTNEIGHBOR",
        "LEAKYRELU",
        "SQUAREDDIFFERENCE",
        "MIRRORPAD",
        "ABS",
        "SPLITV",
        "UNIQUE",
        "CEIL",
        "REVERSEV2",
        "ADDN",
        "GATHERND",
        "COS",
        "WHERE",
        "RANK",
        "ELU",
        "REVERSESEQUENCE",
        "MATRIXDIAG",
        "QUANTIZE",
        "MATRIXSETDIAG",
        "ROUND",
        "HARDSWISH",
        "IF",
        "WHILE",
        "NONMAXSUPPRESSIONV4",
        "NONMAXSUPPRESSIONV5",
        "SCATTERND",
        "SELECTV2",
        "DENSIFY",
        "SEGMENTSUM",
        "BATCHMATMUL",
        "PLACEHOLDERFORGREATEROPCODES",
        "CUMSUM",
    };
    return op >= 0 && op < count ? names[op] : "UNKNOWN";
}

class NodeSupport {
    TfLiteContext *context_;
    TfLiteNode *node_;
    TfLiteRegistration *registration_;
    bool const verbose_;
    mutable std::ostringstream failures_;

    // static const[expr] is a better choice but requires C++17 to avoid pain.
    enum PossibleTypesMask {
        NONE = 1 << kTfLiteNoType,
        U8 = 1 << kTfLiteUInt8,
        I8 = 1 << kTfLiteInt8,
        I16 = 1 << kTfLiteInt16,
        I32 = 1 << kTfLiteInt32,
        F32 = 1 << kTfLiteFloat32,
        F64 = 1 << kTfLiteFloat64,
        BOOLTYPE = 1 << kTfLiteBool,
        I32_OR_NONE = I32 | NONE,
        ANY_ARITHMETIC = U8 | I8 | I16 | I32 | F32 | F64,
        ANY = (int)0xffffffff
    };

    static std::string mask_to_string(int m) {
        // Correspond to the types in TfLiteType
        static const char *const names[32] = {
            "NoType",
            "Float32",
            "Int32",
            "UInt8",
            "Int64",
            "String",
            "Bool",
            "Int16",
            "Complex64",
            "Int8",
            "Float16",
            "Float64",
            "Complex128",
            "Unknown13",
            "Unknown14",
            "Unknown15",
            "Unknown16",
            "Unknown17",
            "Unknown18",
            "Unknown19",
            "Unknown20",
            "Unknown21",
            "Unknown22",
            "Unknown23",
            "Unknown24",
            "Unknown25",
            "Unknown26",
            "Unknown27",
            "Unknown28",
            "Unknown29",
            "Unknown30",
            "Unknown31",
        };
        std::ostringstream o;
        const char *sep = "";
        for (int i = 0; i < 32; i++) {
            if (m & (1 << i)) {
                o << sep << names[i];
                sep = "|";
            }
        }
        return o.str();
    };

    bool DimsAllOk(TfLiteIntArray *list, const char *label) const {
        for (int i = 0; i < list->size; ++i) {
            const int tensor_id = list->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            const TfLiteTensor &tensor = context_->tensors[tensor_id];
            assert(tensor.dims);
            if (tensor.dims->size > 4) {
                if (verbose_) {
                    failures_ << "The " << label << "[" << i << "] has too many dimensions (" << tensor.dims->size << ")\n";
                }
                return false;
            }
        }
        return true;
    }

    bool HasTypeImpl(int i, int possible_types_mask, TfLiteIntArray *list, const char *label) const {
        const int tensor_id = list->data[i];
        if (tensor_id == kTfLiteOptionalTensor) {
            return true;
        }
        const TfLiteTensor &tensor = context_->tensors[tensor_id];
        const int tensor_type_mask = 1 << tensor.type;
        if (!(tensor_type_mask & possible_types_mask)) {
            if (verbose_) {
                failures_ << "For " << label << "[" << i << "]"
                          << ", expected type(s) " << mask_to_string(possible_types_mask)
                          << " but saw " << mask_to_string(tensor_type_mask)
                          << "\n";
            }
            return false;
        }
        return true;
    }

    bool InputHasType(int i, int possible_types_mask) const {
        return HasTypeImpl(i, possible_types_mask, node_->inputs, "input");
    }

    bool OutputHasType(int i, int possible_types_mask) const {
        return HasTypeImpl(i, possible_types_mask, node_->outputs, "output");
    }

    bool ListHasCorrectTypesImpl(std::initializer_list<int> per_tensor_possible_types_mask, TfLiteIntArray *list, const char *label) const {
        if (list->size != (int)per_tensor_possible_types_mask.size()) {
            if (verbose_) {
                failures_ << "Expected " << per_tensor_possible_types_mask.size()
                          << " " << label << "(s) but saw " << list->size << "\n";
            }
            return false;
        }
        int i = -1;
        for (int possible_types_mask : per_tensor_possible_types_mask) {
            ++i;
            if (!HasTypeImpl(i, possible_types_mask, list, label)) {
                return false;
            }
        }
        return true;
    }

    bool InputsHaveCorrectTypes(std::initializer_list<int> per_tensor_possible_types_mask) const {
        return ListHasCorrectTypesImpl(per_tensor_possible_types_mask, node_->inputs, "input");
    }

    bool OutputsHaveCorrectTypes(std::initializer_list<int> per_tensor_possible_types_mask) const {
        return ListHasCorrectTypesImpl(per_tensor_possible_types_mask, node_->outputs, "output");
    }

    bool IsActivationReluOrNone(TfLiteFusedActivation activation) const {
        if (activation == kTfLiteActRelu ||
            activation == kTfLiteActRelu6 ||
            activation == kTfLiteActReluN1To1 ||
            activation == kTfLiteActNone) {
            return true;
        }
        if (verbose_) {
            failures_ << "Activation was expected to be ReluOrNone but was " << activation << "\n";
        }
        return false;
    }

    bool IsVersionOK(int min_version, int max_version) const {
        if (registration_->version < min_version || registration_->version > max_version) {
            if (verbose_) {
                failures_ << "Version " << registration_->version
                          << " is not within range " << min_version << ".." << max_version << "\n";
            }
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Add() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8 | I32, U8 | I32})) {
            return false;
        }
        const TfLiteAddParams *params = (const TfLiteAddParams *)(node_->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Sub() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8 | I32, U8 | I32})) {
            return false;
        }
        const TfLiteSubParams *params = (const TfLiteSubParams *)(node_->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Mul() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8 | I32, U8 | I32})) {
            return false;
        }
        const TfLiteMulParams *params = (const TfLiteMulParams *)(node_->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Compare() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY_ARITHMETIC, ANY_ARITHMETIC})) {
            return false;
        }
        if (!OutputsHaveCorrectTypes({BOOLTYPE})) {
            return false;
        }
        const TfLiteTensor &output = context_->tensors[node_->outputs->data[0]];
        if (output.dims->size != 0) {
            if (verbose_) {
                failures_ << "Output must be a scalar\n";
            }
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
        if (!IsVersionOK(1, 2)) {
            return false;
        }

        if (node_->inputs->size < 1) {
            if (verbose_) {
                failures_ << "Expected at least one input\n";
            }
            return false;
        }

        // All the inputs (and the single output) must match types.
        const int tensor_id = node_->inputs->data[0];
        const TfLiteType type = context_->tensors[tensor_id].type;
        const PossibleTypesMask required_type_mask = PossibleTypesMask(1 << type);
        for (int i = 0; i < node_->inputs->size; ++i) {
            if (!InputHasType(i, required_type_mask)) {
                return false;
            }
        }

        // Exactly one output.
        if (!OutputsHaveCorrectTypes({PossibleTypesMask(1 << type)})) {
            return false;
        }

        // TODO: This op has an activation but we don't appear to use it.
        // const TfLiteConcatenationParams *params = (const TfLiteConcatenationParams *)(node_->builtin_data);
        return true;
    }

    bool IsNodeSupported_Split() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }

        if (!InputsHaveCorrectTypes({I32, ANY})) {
            return false;
        }

        // All the outputs (and the single input) must match types.
        const int tensor_id = node_->inputs->data[1];
        const TfLiteType type = context_->tensors[tensor_id].type;
        const PossibleTypesMask required_type_mask = PossibleTypesMask(1 << type);
        for (int i = 0; i < node_->outputs->size; ++i) {
            if (!OutputHasType(i, required_type_mask)) {
                return false;
            }
        }

        // Exactly one input.

        return true;
    }

    bool IsNodeSupported_SplitV() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }

        if (!InputsHaveCorrectTypes({ANY, I32, I32})) {
            return false;
        }

        // All the outputs (and the single input) must match types.
        const int tensor_id = node_->inputs->data[0];
        const TfLiteType type = context_->tensors[tensor_id].type;
        const PossibleTypesMask required_type_mask = PossibleTypesMask(1 << type);
        for (int i = 0; i < node_->outputs->size; ++i) {
            if (!OutputHasType(i, required_type_mask)) {
                return false;
            }
        }

        return true;
    }

    bool IsNodeSupported_Gather() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY, I32})) {
            return false;
        }
        const TfLiteGatherParams *params = (const TfLiteGatherParams *)(node_->builtin_data);
        if (params->batch_dims != 0) {
            // TODO: we don't support other values for this yet, but we should.
            return false;
        }
        return true;
    }

    // TODO: support GATHER_ND once we find a testcase for it
    // bool IsNodeSupported_GatherNd() const {
    //     if (!IsVersionOK(1, 2)) {
    //         return false;
    //     }
    //     if (!InputsHaveCorrectTypes({ANY, I32})) {
    //         return false;
    //     }
    //     return true;
    // }

    bool IsNodeSupported_Conv2d() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8, I32})) {
            return false;
        }
        const TfLiteConvParams *params = (const TfLiteConvParams *)(node_->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_DepthwiseConv2d() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, U8, I32})) {
            return false;
        }
        const TfLiteDepthwiseConvParams *params = (const TfLiteDepthwiseConvParams *)(node_->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_FullyConnected() const {
        // This is correct, we don't handle the params for v2 or later yet
        if (!IsVersionOK(1, 1)) {
            return false;
        }
        if (!(InputsHaveCorrectTypes({U8, U8, I32_OR_NONE}) && OutputsHaveCorrectTypes({U8})) &&
            // Not sure if this combination is actually expected, but models in the wild
            // require it, so we'll support it
            !(InputsHaveCorrectTypes({U8, U8, I32_OR_NONE}) && OutputsHaveCorrectTypes({I16}))) {
            return false;
        }
        const TfLiteFullyConnectedParams *params = (const TfLiteFullyConnectedParams *)(node_->builtin_data);
        if (!IsActivationReluOrNone(params->activation)) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Pool2d() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        const TfLitePoolParams *params = (const TfLitePoolParams *)(node_->builtin_data);
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
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, I32})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Reshape() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        // Note that Reshape can have 1 or 2 inputs.
        if (node_->inputs->size > 2) {
            if (verbose_) {
                failures_ << "Reshape must have 1 or 2 inputs\n";
            }
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Shape() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Softmax() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_L2Normalization() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Unary() const {
        if (!IsVersionOK(1, 2)) {
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
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({U8, I32})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_SpaceToDepth() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_DepthToSpace() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Transpose() const {
        if (!IsVersionOK(1, 2)) {
            return false;
        }
        if (!InputsHaveCorrectTypes({ANY, I32})) {
            return false;
        }
        return true;
    }

    bool IsNodeSupported_Lstm() const {
        // TODO: we might work with v3 or v4, but haven't tested any instances.
        if (!IsVersionOK(1, 2)) {
            return false;
        }

        // Our 'Lstm' op is actually a group of several Hannk ops;
        // we must check these carefully (see lower_tflite_lstm() for reference):

        if (!InputsHaveCorrectTypes({
                /*data_input*/ U8,
                /*prev_activ_input*/ U8,
                /*weights_input*/ U8,
                /*biases_input*/ I32,
                /*prev_state_input*/ I16,
            })) {
            return false;
        }
        if (!OutputsHaveCorrectTypes({
                /*activ_output*/ U8,
                /*state_output*/ I16,
                /*concat_temp*/ U8,
                /*activ_temp*/ I16,
            })) {
            return false;
        }

        const TfLiteLSTMParams *params = (const TfLiteLSTMParams *)(node_->builtin_data);
        // TODO: there is an activation function specified here but it's not clear
        // whether it's used in the LSTM reference implementation. Ignoring for now.
        // if (params->activation == ...) {
        //     return false;
        // }

        // TODO: for v2+, you can specify 'basic' vs 'full' kernels.
        // The 'basic' kernel is all we've tested with.
        if (registration_->version >= 2) {
            if (params->kernel_type != kTfLiteLSTMBasicKernel) {
                if (verbose_) {
                    failures_ << "LSTM only supports kTfLiteLSTMBasicKernel\n";
                }
                return false;
            }
        }

        return true;
    }

public:
    NodeSupport(TfLiteContext *c, TfLiteNode *n, TfLiteRegistration *r, bool v)
        : context_(c), node_(n), registration_(r), verbose_(v) {
    }

    bool IsNodeSupported() const {
        // Ensure all inputs & outputs have dim <= 4.
        if (!DimsAllOk(node_->inputs, "input")) {
            return false;
        }
        if (!DimsAllOk(node_->outputs, "output")) {
            return false;
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
        switch (registration_->builtin_code) {

            #define KNOWN_OP(OP) case kTfLiteBuiltin##OP: return IsNodeSupported_##OP();
            ALL_KNOWN_OPS
            #undef KNOWN_OP

        default:
            if (verbose_) {
                failures_ << "Op with builtin_code " << registration_->builtin_code << " (" << GetOpName(registration_->builtin_code) << ") is not supported by hannk.\n";
            }
            return false;
        }
        // clang-format on

        return false;
    }

    std::string Failures() const {
        return failures_.str();
    }
};

/*static*/ TfLiteStatus HannkDelegate::DelegatePrepare(TfLiteContext *context, TfLiteDelegate *delegate) {
    HannkDelegate *self = (HannkDelegate *)delegate;
    const int verbosity = self->options_.verbosity;

    TfLiteStatus status;

    TfLiteIntArray *plan = nullptr;
    if ((status = context->GetExecutionPlan(context, &plan)) != kTfLiteOk) {
        HLOG(ERROR) << "GetExecutionPlan failed";
        return status;
    }

    // Build up a list of the nodes we want to handle.
    std::vector<int> supported_nodes;
    for (int i = 0; i < plan->size; i++) {
        const int node_index = plan->data[i];
        TfLiteNode *node;
        TfLiteRegistration *registration;
        if ((status = context->GetNodeAndRegistration(context, node_index, &node, &registration)) != kTfLiteOk) {
            HLOG(ERROR) << "GetNodeAndRegistration failed";
            return status;
        }

        NodeSupport support(context, node, registration, verbosity >= 1);
        if (support.IsNodeSupported()) {
            if (verbosity >= 2) {
                HLOG(INFO) << "Handling node, index=" << node_index
                           << " code=" << registration->builtin_code
                           << " (" << GetOpName(registration->builtin_code) << ")";
            }
            supported_nodes.push_back(node_index);
        } else {
            if (verbosity >= 1) {
                HLOG(INFO) << "Skipping unsupported node, index=" << node_index
                           << " code=" << registration->builtin_code
                           << " (" << GetOpName(registration->builtin_code) << ")"
                           << " version=" << registration->version
                           << " custom_name=(" << (registration->custom_name ? registration->custom_name : "null") << "); "
                           << "Reason(s): " << support.Failures();
            }
        }
    }

    if ((status = context->ReplaceNodeSubsetsWithDelegateKernels(context,
                                                                 HannkDelegateKernel::GetRegistration(),
                                                                 BuildTfLiteIntArray(supported_nodes).get(),
                                                                 delegate)) != kTfLiteOk) {
        HLOG(ERROR) << "ReplaceNodeSubsetsWithDelegateKernels failed";
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
