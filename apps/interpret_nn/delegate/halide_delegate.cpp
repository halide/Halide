#include "delegate/halide_delegate.h"

#include <memory>
#include <string>
#include <vector>

#include "interpreter/interpreter.h"
#include "interpreter/ops.h"
#include "tensorflow/lite/builtin_ops.h"
#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/c/c_api.h"
#include "util/error_util.h"

#define ALLOW_DYNAMIC_TENSORS 0

// Use a List-Of-X approach here to ensure that places we handle ops are kept in sync
#define ALL_KNOWN_OPS         \
    KNOWN_OP(Add)             \
    KNOWN_OP(AveragePool2d)   \
    KNOWN_OP(Concatenation)   \
    KNOWN_OP(Conv2d)          \
    KNOWN_OP(DepthwiseConv2d) \
    KNOWN_OP(FullyConnected)  \
    KNOWN_OP(MaxPool2d)       \
    KNOWN_OP(Pad)             \
    KNOWN_OP(Reshape)         \
    KNOWN_OP(Quantize)

namespace interpret_nn {
namespace {

constexpr char kDelegateName[] = "HalideDelegate";
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
        ::interpret_nn::TfLiteIntArrayFree(a);
    }
};

std::unique_ptr<TfLiteIntArray, TfLiteIntArrayDeleter> BuildTfLiteIntArray(const std::vector<int> &data) {
    std::unique_ptr<TfLiteIntArray, TfLiteIntArrayDeleter> result(TfLiteIntArrayCreate(data.size()));
    std::copy(data.begin(), data.end(), result->data);
    return result;
}

// -------------------- HalideDelegate

struct HalideDelegate final : public TfLiteDelegate {
    explicit HalideDelegate(const HalideDelegateOptions *p)
        : TfLiteDelegate(),
          options_(p != nullptr ? *p : HalideDelegateOptions()) {
        assert(this->data_ == nullptr);
        assert(this->CopyFromBufferHandle == nullptr);
        assert(this->CopyToBufferHandle == nullptr);
        assert(this->FreeBufferHandle == nullptr);
        this->Prepare = DelegatePrepare;
#if ALLOW_DYNAMIC_TENSORS
        this->flags = kTfLiteDelegateFlagsAllowDynamicTensors;
#else
        this->flags = 0;
#endif
    }

    static TfLiteStatus DelegatePrepare(TfLiteContext *context, TfLiteDelegate *delegate);

    const HalideDelegateOptions options_;
};

// -------------------- HalideDelegateKernel

TensorType ConvertTfLiteType(TfLiteType t) {
    // Note that TfLiteType has different numerical values from the
    // similar enum found in tflite flatbuffers.
    switch (t) {
    case kTfLiteFloat32:
        return TensorType::Float32;
    case kTfLiteFloat16:
        return TensorType::Float16;
    case kTfLiteInt32:
        return TensorType::Int32;
    case kTfLiteUInt8:
        return TensorType::UInt8;
    case kTfLiteInt64:
        return TensorType::Int64;
    case kTfLiteString:
        return TensorType::String;
    case kTfLiteBool:
        return TensorType::Bool;
    case kTfLiteInt16:
        return TensorType::Int16;
    case kTfLiteComplex64:
        return TensorType::Complex64;
    case kTfLiteInt8:
        return TensorType::Int8;
    case kTfLiteFloat64:
        return TensorType::Float64;
    case kTfLiteNoType:
        CHECK(0) << "kTfLiteNoType is not supported";
    default:
        CHECK(0) << "Unknown TfLiteType";
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

std::vector<halide_dimension_t> ConvertTfLiteShape(const TfLiteTensor &tensor, size_t *shape_size_out = nullptr) {
    std::vector<halide_dimension_t> shape(tensor.dims->size);
    size_t shape_size = 1;
    for (int i = 0; i < (int)shape.size(); i++) {
        shape[i].min = 0;
        shape[i].extent = tensor.dims->data[shape.size() - 1 - i];
        shape[i].stride = shape_size;
        shape_size *= shape[i].extent;
    }
    if (shape_size_out) {
        *shape_size_out = shape_size;
    }
    return shape;
}

std::shared_ptr<Tensor> ConvertTfLiteTensor(const TfLiteTensor &tensor) {
    // TODO: this always makes a copy of the tensor data; we should be able to just
    // shadow it. Needs some refactoring in Tensor (at least) to make work.
    std::vector<uint8_t> data;
    if (tensor.allocation_type == kTfLiteMmapRo) {
        const uint8_t *d = (const uint8_t *)tensor.data.data;
        data.assign(d, d + tensor.bytes);
    }

    size_t shape_size;
    auto shape = ConvertTfLiteShape(tensor, &shape_size);

    TensorType type = ConvertTfLiteType(tensor.type);
    assert(data.empty() || data.size() == shape_size * sizeof_tensor_type(type));

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
    return std::make_shared<Tensor>(name, type, std::move(shape),
                                    std::move(data), std::move(quantization));
}

class HalideDelegateKernel final {
public:
    // Each kernel instance will be used from only a single thread.
    // (It is fine for the kernel itself to use multiple threads internally.)
    explicit HalideDelegateKernel(const HalideDelegateOptions &options)
        : options_(options) {
    }

    // Init() will be called exactly once per instance.
    TfLiteStatus Init(TfLiteContext *context,
                      const TfLiteDelegateParams *params) {
        if (model_ != nullptr || interpreter_ != nullptr) {
            TF_LITE_KERNEL_LOG(context, "Init must not be called twice.");
            return kTfLiteError;
        }
        model_ = make_unique<Model>();

        std::vector<int> node_indices(params->nodes_to_replace->size);
        for (int i = 0; i < params->nodes_to_replace->size; i++) {
            const int node_index = params->nodes_to_replace->data[i];
            node_indices[i] = node_index;
        }
        LOG(INFO) << "Delegate " << (void *)this << " Init nodes: " << node_indices << "\n";

        // Pre-emptively map *all* the TFLiteTensors into our Tensor type.
        for (size_t tensor_id = 0; tensor_id < context->tensors_size; tensor_id++) {
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            auto t = ConvertTfLiteTensor(tensor);
            model_->tensors.emplace_back(t);
            assert(!tensor_id_to_tensor_ptr_.count(tensor_id));
            tensor_id_to_tensor_ptr_[tensor_id] = t;
            // LOG(INFO) << "tensor_id " << tensor_id << " -> " << (void*) t.get() << "\n";
        }

        // Be careful with params->input_tensors and params->output_tensors here;
        // in particular, params->input_tensors will contain all of the 'constant'
        // input tensors (which are generally inputs only to a specific node).
#if ALLOW_DYNAMIC_TENSORS
        // TODO: verify the above comment is still correct.
#endif

        // Mark the input and output tensors correctly, as code in our interpreter
        // relies upon it. TODO: verify that is necessary.
        for (int i = 0; i < params->input_tensors->size; i++) {
            const int tensor_id = params->input_tensors->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            auto t = GetTensorById(context, tensor_id);
            t->set_input(true);
            // LOG(INFO) << "Delegate " << (void *)this << (t->is_constant() ? " Const" : "") << " Input tensor: " << tensor_id << "\n";
        }

        // Add the output tensors.
        for (int i = 0; i < params->output_tensors->size; i++) {
            const int tensor_id = params->output_tensors->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            // LOG(INFO) << "Delegate " << (void *)this << " Output tensor: " << tensor_id << "\n";
            auto t = GetTensorById(context, tensor_id);
            t->set_output(true);
        }

        // Add all ops.
        TfLiteNode *node;
        TfLiteRegistration *reg;
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
                return kTfLiteError;
            }
            // clang-format on

            if (op == nullptr) {
                TF_LITE_KERNEL_LOG(context, "Op factory returned null: %s", op_type);
                return kTfLiteError;
            }
            model_->ops.emplace_back(std::move(op));
        }

        return kTfLiteOk;
    }

    // Prepare() will be called at least once, prior to any calls to Eval().
    // It will be called again if tensor shape(s) change. It is preferable
    // to do all memory allocation in Prepare(), rather than Eval(), if possible.
    TfLiteStatus Prepare(TfLiteContext *context, TfLiteNode *node) {
        LOG(INFO) << "Delegate " << (void *)this << " Prepare\n";

        assert(model_ != nullptr);

#if ALLOW_DYNAMIC_TENSORS
        // Because we set kTfLiteDelegateFlagsAllowDynamicTensors, TFLite
        // may call Prepare() after Eval() if only tensor shapes have changed
        // (but nothing else in the model), which is a nice potential optimization.
        // (Apparently, if you don't set kTfLiteDelegateFlagsAllowDynamicTensors,
        // TFLite will create a fresh Delegate for every call instead.)
        //
        // TODO: will be called with interp (but no model) if inputs resized.
        // update the tensors in the model/interp.
        abort();  // TODO
#else
        if (interpreter_ != nullptr) {
            TF_LITE_KERNEL_LOG(context, "Calling Prepare() multiple times");
            return kTfLiteError;
        }
#endif

        interpreter_ = make_unique<ModelInterpreter>(std::move(*model_));
        model_.reset();
        return kTfLiteOk;
    }

    // Eval() will be called at least once. It can expect that prepare() will
    // have been called for the current set of tensor shape(s).
    TfLiteStatus Eval(TfLiteContext *context, TfLiteNode *node) {
        LOG(INFO) << "Delegate " << (void *)this << " Eval\n";

        if (interpreter_ == nullptr) {
            TF_LITE_KERNEL_LOG(context, "interpreter_ is not built in Eval");
            return kTfLiteError;
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
            assert(t->is_constant() == (tensor.allocation_type == kTfLiteMmapRo));
            if (t->is_constant()) {
                continue;
            }
            assert(t->is_input() && !t->is_constant() && t->is_allocated());
            auto buf = t->data<void>();
            assert(buf.size_in_bytes() == tensor.bytes);

            memcpy(buf.data(), tensor.data.data, tensor.bytes);
        }

        // TODO: execute needs to return an error code.
        halide_set_num_threads(options_.num_threads);
        interpreter_->execute();

        // Copy the Tensor outputs. TODO: avoid this by sharing pointers.
        for (int i = 0; i < node->outputs->size; i++) {
            const int tensor_id = node->outputs->data[i];
            if (tensor_id == kTfLiteOptionalTensor) {
                continue;
            }
            assert(tensor_id >= 0 && tensor_id < (int)context->tensors_size);
            const TfLiteTensor &tensor = context->tensors[tensor_id];
            assert(tensor.allocation_type != kTfLiteMmapRo);
            auto t = GetTensorById(context, tensor_id);
            assert(t->is_output() && !t->is_constant() && t->is_allocated());
            auto buf = t->data<const void>();
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
            LOG(ERROR) << "HalideDelegate.init: NULL params";
            return nullptr;
        }
        HalideDelegate *halide_delegate = (HalideDelegate *)params->delegate;
        auto self = make_unique<HalideDelegateKernel>(halide_delegate->options_);
        if (self->Init(context, params) != kTfLiteOk) {
            LOG(ERROR) << "HalideDelegate.init: NULL params";
            return nullptr;
        }
        return self.release();
    };

    static void FreeImpl(TfLiteContext *context, void *buffer) {
        HalideDelegateKernel *self = (HalideDelegateKernel *)buffer;
        delete self;
    };

    static TfLiteStatus PrepareImpl(TfLiteContext *context, TfLiteNode *node) {
        if (node->user_data == nullptr) {
            LOG(ERROR) << "Delegate kernel was not initialized";
            return kTfLiteError;
        }
        HalideDelegateKernel *self = (HalideDelegateKernel *)node->user_data;
        return self->Prepare(context, node);
    };

    static TfLiteStatus InvokeImpl(TfLiteContext *context, TfLiteNode *node) {
        HalideDelegateKernel *self = (HalideDelegateKernel *)node->user_data;
        assert(self != nullptr);
        return self->Eval(context, node);
    };

    Tensor *GetTensorById(TfLiteContext *context, int tensor_id) {
        auto it = tensor_id_to_tensor_ptr_.find(tensor_id);
        if (it == tensor_id_to_tensor_ptr_.end()) {
            LOG(ERROR) << "tensor_id not found: " << tensor_id;
            return nullptr;
        }
        return it->second.get();
    }

    std::unique_ptr<Op> BuildAdd(TfLiteContext *context, TfLiteNode *node) {
        auto input1 = GetTensorById(context, node->inputs->data[0]);
        auto input2 = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteAddParams *params = (const TfLiteAddParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        return make_unique<AddOp>(input1, input2, output, activation);
    }

    std::unique_ptr<Op> BuildAveragePool2d(TfLiteContext *context, TfLiteNode *node) {
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
        return make_unique<AveragePoolOp>(input, output, stride, filter_size, padding, activation);
    }

    std::unique_ptr<Op> BuildConcatenation(TfLiteContext *context, TfLiteNode *node) {
        std::vector<Tensor *> inputs(node->inputs->size);
        for (int i = 0; i < node->inputs->size; i++) {
            inputs[i] = GetTensorById(context, node->inputs->data[i]);
        }
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteConcatenationParams *params = (const TfLiteConcatenationParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        int axis = params->axis;
        // Handle negative values, which are legal
        if (axis < 0) {
            axis = (int)output->shape().size() + axis;
        }
        // Now 'flip' the axis so that it refers to the right dimension in
        // the Tensor (since we reverse the dimension order)
        axis = (int)output->shape().size() - axis - 1;
        return make_unique<ConcatenationOp>(inputs, output, axis, activation);
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
        return make_unique<Conv2DOp>(input, filter, bias, output, stride,
                                     dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> BuildDepthwiseConv2d(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteDepthwiseConvParams *params = (const TfLiteDepthwiseConvParams *)(node->builtin_data);
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
        // TODO: depth_multiplier is considered redundant and semi-deprecated;
        // see buildin_op_data.h in tflite source for more info.
        int depth_multiplier = params->depth_multiplier;
        return make_unique<DepthwiseConv2DOp>(input, filter, bias, output, depth_multiplier, stride,
                                              dilation_factor, padding, activation);
    }

    std::unique_ptr<Op> BuildFullyConnected(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto filter = GetTensorById(context, node->inputs->data[1]);
        auto bias = GetTensorById(context, node->inputs->data[2]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteFullyConnectedParams *params = (const TfLiteFullyConnectedParams *)(node->builtin_data);
        auto activation = ConvertTfLiteActivation(params->activation);
        return make_unique<FullyConnectedOp>(input, filter, bias, output, activation);
    }

    std::unique_ptr<Op> BuildMaxPool2d(TfLiteContext *context, TfLiteNode *node) {
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
        return make_unique<MaxPoolOp>(input, output, stride, filter_size, padding, activation);
    }

    std::unique_ptr<Op> BuildPad(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto padding = GetTensorById(context, node->inputs->data[1]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return make_unique<PadOp>(input, padding, output);
    }

    std::unique_ptr<Op> BuildReshape(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        const TfLiteReshapeParams *params = (const TfLiteReshapeParams *)(node->builtin_data);
        std::vector<int> new_shape;
        new_shape.assign(params->shape, params->shape + params->num_dimensions);
        return make_unique<ReshapeOp>(input, output, new_shape);
    }

    std::unique_ptr<Op> BuildQuantize(TfLiteContext *context, TfLiteNode *node) {
        auto input = GetTensorById(context, node->inputs->data[0]);
        auto output = GetTensorById(context, node->outputs->data[0]);
        return make_unique<QuantizeOp>(input, output);
    }

    const HalideDelegateOptions options_;
    std::unique_ptr<Model> model_;
    std::unique_ptr<ModelInterpreter> interpreter_;
    std::map<int, std::shared_ptr<Tensor>> tensor_id_to_tensor_ptr_;
};

bool InputsHaveCorrectTypes(const TfLiteNode *node,
                            TfLiteContext *context,
                            std::initializer_list<int> per_input_possible_types_mask) {
    if (node->inputs->size != (int)per_input_possible_types_mask.size()) {
        LOG(ERROR) << "inputs size mismatch in InputsHaveCorrectTypes";
        return false;
    }
    int i = -1;
    for (int possible_types_mask : per_input_possible_types_mask) {
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

bool AllInputsHaveType(const TfLiteNode *node, TfLiteContext *context, int possible_types_mask) {
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

bool IsActivationReluOrNone(TfLiteFusedActivation activation) {
    return (activation == kTfLiteActRelu ||
            activation == kTfLiteActRelu6 ||
            activation == kTfLiteActReluN1To1 ||
            activation == kTfLiteActNone);
}

// TODO: this should also allow Int8 once we fix biasing for those
constexpr int k8BitMask = 1 << kTfLiteUInt8;

bool IsNodeSupported_Add(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask, k8BitMask})) {
        return false;
    }
    const TfLiteAddParams *params = (const TfLiteAddParams *)(node->builtin_data);
    if (!IsActivationReluOrNone(params->activation)) {
        return false;
    }
    return true;
}

bool IsNodeSupported_AveragePool2d(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask})) {
        return false;
    }
    const TfLitePoolParams *params = (const TfLitePoolParams *)(node->builtin_data);
    if (!IsActivationReluOrNone(params->activation)) {
        return false;
    }
    return true;
}

bool IsNodeSupported_Concatenation(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!AllInputsHaveType(node, context, k8BitMask)) {
        return false;
    }
    // TODO: This op has an activation but we don't appear to use it.
    // const TfLiteConcatenationParams *params = (const TfLiteConcatenationParams *)(node->builtin_data);
    return true;
}

bool IsNodeSupported_Conv2d(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask, k8BitMask, 1 << kTfLiteInt32})) {
        return false;
    }
    const TfLiteConvParams *params = (const TfLiteConvParams *)(node->builtin_data);
    if (!IsActivationReluOrNone(params->activation)) {
        return false;
    }
    return true;
}

bool IsNodeSupported_DepthwiseConv2d(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask, k8BitMask, 1 << kTfLiteInt32})) {
        return false;
    }
    const TfLiteDepthwiseConvParams *params = (const TfLiteDepthwiseConvParams *)(node->builtin_data);
    if (!IsActivationReluOrNone(params->activation)) {
        return false;
    }
    return true;
}

bool IsNodeSupported_FullyConnected(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    // This is correct, we don't handle the params for v2 or later yet
    if (!(registration->version <= 1)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask, k8BitMask, (1 << kTfLiteInt32) | (1 << kTfLiteNoType)})) {
        return false;
    }
    const TfLiteFullyConnectedParams *params = (const TfLiteFullyConnectedParams *)(node->builtin_data);
    if (!IsActivationReluOrNone(params->activation)) {
        return false;
    }
    return true;
}

bool IsNodeSupported_MaxPool2d(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask})) {
        return false;
    }
    const TfLitePoolParams *params = (const TfLitePoolParams *)(node->builtin_data);
    if (!IsActivationReluOrNone(params->activation)) {
        return false;
    }
    return true;
}

bool IsNodeSupported_Pad(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask, 1 << kTfLiteInt32})) {
        return false;
    }
    return true;
}

bool IsNodeSupported_Reshape(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    // Note that Reshape can have 1 or 2 inputs.
    if (node->inputs->size > 2) {
        return false;
    }
    return true;
}

bool IsNodeSupported_Quantize(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    if (!(registration->version <= 2)) {
        return false;
    }
    if (!InputsHaveCorrectTypes(
            node, context,
            {k8BitMask})) {
        return false;
    }
    return true;
}

bool IsNodeSupported(TfLiteContext *context, TfLiteNode *node, TfLiteRegistration *registration) {
    // Ensure all inputs & outputs have dim <= 4.
    for (int i = 0; i < node->inputs->size; ++i) {
        const int tensor_id = node->inputs->data[i];
        if (tensor_id == kTfLiteOptionalTensor) {
            continue;
        }
        const TfLiteTensor &tensor = context->tensors[tensor_id];
        if (tensor.dims->size > 4) {
            return false;
        }
    }
    for (int i = 0; i < node->outputs->size; ++i) {
        const int tensor_id = node->outputs->data[i];
        const TfLiteTensor &tensor = context->tensors[tensor_id];
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

        #define KNOWN_OP(OP) case kTfLiteBuiltin##OP: return IsNodeSupported_##OP(context, node, registration);
        ALL_KNOWN_OPS
        #undef KNOWN_OP

    default:
        return false;
    }
    // clang-format on

    return false;
}

/*static*/ TfLiteStatus HalideDelegate::DelegatePrepare(TfLiteContext *context, TfLiteDelegate *delegate) {
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

        if (IsNodeSupported(context, node, registration)) {
            supported_nodes.push_back(node_index);
        } else {
            LOG(INFO) << "NODE REJECTED: " << node_index << "\n";
        }
    }

    if ((status = context->ReplaceNodeSubsetsWithDelegateKernels(context,
                                                                 HalideDelegateKernel::GetRegistration(),
                                                                 BuildTfLiteIntArray(supported_nodes).get(),
                                                                 delegate)) != kTfLiteOk) {
        LOG(ERROR) << "ReplaceNodeSubsetsWithDelegateKernels failed";
        return status;
    }

    return kTfLiteOk;
}

}  // namespace
}  // namespace interpret_nn

TfLiteDelegate *HalideDelegateCreate(const HalideDelegateOptions *options) {
    using interpret_nn::HalideDelegate;

    return new HalideDelegate(options);
}

void HalideDelegateOptionsDefault(HalideDelegateOptions *opt) {
    *opt = HalideDelegateOptions();
}

void HalideDelegateDelete(TfLiteDelegate *delegate) {
    using interpret_nn::HalideDelegate;

    if (delegate) {
        HalideDelegate *halide_delegate = (HalideDelegate *)delegate;
        delete halide_delegate;
    }
}
