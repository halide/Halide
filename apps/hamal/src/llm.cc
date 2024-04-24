#include "llm.h"

#include "absl/strings/str_cat.h"
#include "hamal_position_embedding.h"
#include "hamal_postprocessor.h"
#include "hamal_preprocessor.h"
#include "hamal_rope_values.h"
#include "hamal_transformer_kv_update_cache.h"
#include "hamal_transformer_kv_use_cache.h"
#include "hamal_transformer_no_kv_cache.h"
#include "src/status_helpers.h"

#define DUMP_INFO_TO_STDOUT 0

namespace hamal {
namespace {

void dump_segpos(const float *data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        std::cout << "data[" << i << "] = " << data[i] << "\n";
    }
}

void PrintBuffer(const std::string &base_name,
                 const Halide::Runtime::Buffer<> &buf) {
#if DUMP_INFO_TO_STDOUT
    std::cout << base_name << ": [";
    const char *prefix = "";
    for (int32_t i = 0; i < buf.dimensions(); i++) {
        std::cout << prefix << "{" << buf.dim(i).min() << ", "
                  << buf.dim(i).extent() << "}";
        prefix = ", ";
    }
    std::cout << "]\n"
              << std::flush;
#endif
}

void DumpFloatBuffer(const std::string &base_name,
                     const Halide::Runtime::Buffer<> &buf, int dim0_count,
                     int dim1_count = 1) {
#if DUMP_INFO_TO_STDOUT
    PrintBuffer(base_name, buf);
    Halide::Runtime::Buffer<float> temp_buf = buf;
    float *data = temp_buf.data();
    for (int j = temp_buf.dim(1).min();
         j <
         std::min(temp_buf.dim(1).max() + 1, temp_buf.dim(1).min() + dim1_count);
         j++) {
        std::cout << "Start of dump for " << base_name << " (0, " << j << ") "
                  << /*data <<*/ ":\n";
        for (int i = temp_buf.dim(0).min();
             i < std::min(temp_buf.dim(0).max() + 1,
                          temp_buf.dim(0).min() + dim0_count);
             i++) {
            std::cout << "data[" << i << "] = " << data[i] << "\n";
        }
        std::cout << "End of dump for " << base_name << " (0, " << j << "):\n";
        data += temp_buf.dim(1).stride();
    }
    std::cout << std::flush;
#endif
}

void PrintInFloatBuffer2D(const std::string &base_name,
                          const Halide::Runtime::Buffer<> &buf) {
#if DUMP_INFO_TO_STDOUT
    PrintBuffer(base_name, buf);
    const Halide::Runtime::Buffer<float> &fp_buf = buf;
    if (fp_buf.dim(0).extent() > 0) {
        std::cout << base_name << "[0, 0] : " << fp_buf(0, 0) << "\n";
    } else {
        std::cout << base_name << ": empty\n";
        return;
    }
    if (fp_buf.dim(0).extent() > 1) {
        int32_t index = fp_buf.dim(0).extent() - 1;
        std::cout << base_name << "[" << index << ", 0] : " << fp_buf(index, 0)
                  << "\n";
    }
    if (fp_buf.dim(1).extent() > 0) {
        std::cout << base_name << "[0, 1] : " << fp_buf(0, 1) << "\n";
        if (fp_buf.dim(0).extent() > 1) {
            int32_t index = fp_buf.dim(0).extent() - 1;
            std::cout << base_name << "[" << index << ", 1] : " << fp_buf(index, 1)
                      << "\n";
        }
    }
    if (fp_buf.dim(1).extent() > 1) {
        int32_t index_outer = fp_buf.dim(1).extent() - 1;
        std::cout << base_name << "[0, " << index_outer
                  << "] : " << fp_buf(0, index_outer) << "\n";
        if (fp_buf.dim(0).extent() > 1) {
            int32_t index_inner = fp_buf.dim(0).extent() - 1;
            std::cout << base_name << "[" << index_inner << ", " << index_outer
                      << "] : " << fp_buf(index_inner, index_outer) << "\n";
        }
    }
    std::cout << std::flush;
#endif
}

void PrintInFloatBuffer(const std::string &base_name,
                        const Halide::Runtime::Buffer<> &buf) {
#if DUMP_INFO_TO_STDOUT
    PrintBuffer(base_name, buf);
    const Halide::Runtime::Buffer<float> &fp_buf = buf;
    if (fp_buf.dim(0).extent() > 0) {
        std::cout << base_name << "[0, 0, 0] : " << fp_buf(0, 0, 0) << "\n";
    } else {
        std::cout << base_name << ": empty\n";
        return;
    }
    if (fp_buf.dim(0).extent() > 1) {
        int32_t index = fp_buf.dim(0).extent() - 1;
        std::cout << base_name << "[" << index
                  << ", 0, 0] : " << fp_buf(index, 0, 0) << "\n";
    }
    if (fp_buf.dim(1).extent() > 0) {
        std::cout << base_name << "[0, 1, 0] : " << fp_buf(0, 1, 0) << "\n";
        if (fp_buf.dim(0).extent() > 1) {
            int32_t index = fp_buf.dim(0).extent() - 1;
            std::cout << base_name << "[" << index
                      << ", 1, 0] : " << fp_buf(index, 1, 0) << "\n";
        }
    }
    std::cout << std::flush;
#endif
}

}  // anonymous namespace

// Input is not const because the Buffer is filled in with a pointer to the
// data, which is not const.
void TensorToHalideBuffer(std::shared_ptr<Tensor> tensor,
                          Halide::Runtime::Buffer<> *buffer,
                          Halide::Runtime::Buffer<> *scale_buffer = nullptr) {
    std::abort();
    // if (tensor) {
    //     bool need_scale = false;
    //     halide_type_t halide_type;
    //     halide_type.lanes = 1;
    //     switch (tensor->datatype) {
    //     case xnn_datatype_fp32:
    //         halide_type.code = halide_type_float;
    //         halide_type.bits = 32;
    //         break;
    //     case xnn_datatype_fp16:
    //         halide_type.code = halide_type_float;
    //         halide_type.bits = 16;
    //         break;
    //     case xnn_datatype_qint8:
    //         halide_type.code = halide_type_int;
    //         halide_type.bits = 8;
    //         break;
    //     case xnn_datatype_quint8:
    //         halide_type.code = halide_type_uint;
    //         halide_type.bits = 8;
    //         break;
    //     case xnn_datatype_qint32:
    //         halide_type.code = halide_type_int;
    //         halide_type.bits = 32;
    //         break;
    //     case xnn_datatype_qcint8:
    //         halide_type.code = halide_type_int;
    //         halide_type.bits = 8;
    //         need_scale = true;
    //         break;
    //     case xnn_datatype_qcint32:
    //         halide_type.code = halide_type_int;
    //         halide_type.bits = 32;
    //         break;
    //     case xnn_datatype_qcint4:
    //         halide_type.code = halide_type_int;
    //         halide_type.bits = 4;
    //         break;
    //     case xnn_datatype_qdint8:
    //         halide_type.code = halide_type_int;
    //         halide_type.bits = 8;
    //         break;
    //     default:
    //         assert(false);
    //         break;
    //     }

    //     // Halide convention has dims in opposite order of Tensor.
    //     std::vector<int> sizes;
    //     sizes.reserve(tensor->dims.size());
    //     for (size_t i = tensor->dims.size(); i > 0; i--) {
    //         size_t dim = tensor->dims[i - 1];
    //         CHECK_GE(dim, 0);
    //         CHECK_LE(dim, std::numeric_limits<int>::max());
    //         sizes.push_back(static_cast<int>(dim));
    //     }

    //     *buffer = Halide::Runtime::Buffer<>(halide_type, tensor->Data(), sizes);

    //     if (need_scale) {
    //         assert(scale_buffer != nullptr);
    //         // TODO: Surely there is a better way to do this.
    //         QCTensor *qc_tensor =
    //             static_cast<QCTensor *>(tensor.get());
    //         *scale_buffer = Halide::Runtime::Buffer<float>(
    //             qc_tensor->scale_data.get(),
    //             static_cast<int32_t>(qc_tensor->dims[qc_tensor->dim_scale]));
    //     } else if (scale_buffer != nullptr) {
    //         *scale_buffer = Halide::Runtime::Buffer<>();
    //     }
    // } else {
    //     *buffer = Halide::Runtime::Buffer<>();
    //     if (scale_buffer != nullptr) {
    //         *scale_buffer = Halide::Runtime::Buffer<>();
    //     }
    // }
}

absl::StatusOr<std::unique_ptr<Llm>> Llm::CreateLlm(
    std::string weights_folder, const LlmParams &llm_params,
    std::unique_ptr<RuntimeConfigs> runtime_configs) {
    // TODO uncomment when weightsloader is written
    // auto weight_loader = std::make_unique<LlmWeightsLoader>(
    //     weights_folder, llm_params);
    // return CreateLlm(std::move(weight_loader), std::move(runtime_configs));
    std::abort();
    return absl::UnknownError("TODO");
}

void do_indent(int indent) {
    while (indent-- > 0) {
        std::cout << "\t";
    }
}

void PrintTensorInfo(int indent, const std::string &name,
                     const std::shared_ptr<Tensor> &tensor) {
#if DUMP_INFO_TO_STDOUT
    do_indent(indent);
    std::cout << name << ": ";
    if (!tensor) {
        std::cout << "<null tensor>\n";
        return;
    }
    // switch (tensor->datatype) {
    // case xnn_datatype_invalid:
    //     std::cout << "xnn_datatype_invalid ";
    //     break;
    // case xnn_datatype_fp32:
    //     std::cout << "xnn_datatype_fp32 ";
    //     break;
    // case xnn_datatype_fp16:
    //     std::cout << "xnn_datatype_fp16 ";
    //     break;
    // case xnn_datatype_qint8:
    //     std::cout << "xnn_datatype_qint8 ";
    //     break;
    // case xnn_datatype_quint8:
    //     std::cout << "xnn_datatype_quint8 ";
    //     break;
    // case xnn_datatype_qint32:
    //     std::cout << "xnn_datatype_qint32 ";
    //     break;
    // case xnn_datatype_qcint8:
    //     std::cout << "xnn_datatype_qcint8 ";
    //     break;
    // case xnn_datatype_qcint32:
    //     std::cout << "xnn_datatype_qcint32 ";
    //     break;
    // case xnn_datatype_qcint4:
    //     std::cout << "xnn_datatype_qcint4 ";
    //     break;
    // case xnn_datatype_qdint8:
    //     std::cout << "xnn_datatype_qdint8 ";
    //     break;
    // default:
    //     std::cout << "<unknown xnn_datatype "
    //               << static_cast<int>(tensor->datatype) << "> ";
    //     break;
    // }
    // std::cout << "{";
    // const char *prefix = "";
    // for (size_t dim : tensor->dims) {
    //     std::cout << prefix << dim;
    //     prefix = ", ";
    // }
    // std::cout << "}\n";
    // do_indent(indent);
    // std::cout << "info for metadata: " << *tensor << "\n";
#endif
}

void PrintNormWeightInfo(
    int indent, const std::string &name,
    const std::optional<LlmWeights::NormWeights>
        &norm_weights) {
#if DUMP_INFO_TO_STDOUT
    do_indent(indent);
    std::cout << name << ": ";
    if (!norm_weights) {
        std::cout << "<empty>\n";
    } else {
        if (norm_weights->index() == 0) {
            std::cout << "RMS Norm\n";
            PrintTensorInfo(indent + 1, "norm_weight",
                            std::get<0>(*norm_weights).norm_weight);
        } else {
            std::cout << "Layer Norm\n";
            do_indent(indent);
            std::cout << "epsilon: " << std::get<1>(*norm_weights).epsilon << "\n";
            PrintTensorInfo(indent + 1, "gamma", std::get<1>(*norm_weights).gamma);
            PrintTensorInfo(indent + 1, "beta", std::get<1>(*norm_weights).beta);
        }
    }
#endif
}

void Llm::PrintParamsAndWeights() const {
    // #if DUMP_INFO_TO_STDOUT
#if 0
  std::cout << "LLM Params:\n\t";
  std::cout << "num_transformer_M: " << llm_params_.num_transformer_M << "\n\t";
  std::cout << "batch_size_B: " << llm_params_.batch_size_B << "\n\t";
  std::cout << "seq_size_T: " << llm_params_.seq_size_T << "\n\t";
  std::cout << "model_dim_D: " << llm_params_.model_dim_D << "\n\t";
  std::cout << "hidden_dim_HD: " << llm_params_.hidden_dim_HD << "\n\t";
  std::cout << "head_dim_H: " << llm_params_.head_dim_H << "\n\t";
  std::cout << "n_heads_N: " << llm_params_.n_heads_N << "\n\t";
  std::cout << "voc_size_V: " << llm_params_.voc_size_V << "\n\t";
  std::cout << "num_kv_heads: " << llm_params_.num_kv_heads << "\n\t";
  std::cout << "model_type: " << static_cast<int>(llm_params_.model_type)
            << "\n\t";
  std::cout << "skip_absolute_positional_embeddings: "
            << llm_params_.skip_absolute_positional_embeddings << "\n\t";
  std::cout << "sa_params:" << "\n\t\t";
  std::cout << "qkv_no_bias: " << llm_params_.sa_params.qkv_no_bias << "\n\t\t";
  std::cout << "post_proj_no_bias: " << llm_params_.sa_params.post_proj_no_bias
            << "\n\t\t";
  std::cout << "pre_norm: " << static_cast<int>(llm_params_.sa_params.pre_norm)
            << "\n\t\t";
  std::cout << "post_norm: "
            << static_cast<int>(llm_params_.sa_params.post_norm) << "\n\t\t";
  std::cout << "soft_cap_value: " << llm_params_.sa_params.soft_cap_value
            << "\n\t\t";
  std::cout << "attention_scale_type: "
            << static_cast<int>(llm_params_.sa_params.attention_scale_type)
            << "\n\t";
  std::cout << "ff_params:" << "\n\t\t";
  std::cout << "no_bias: " << llm_params_.ff_params.no_bias << "\n\t\t";
  std::cout << "activation: "
            << static_cast<int>(llm_params_.ff_params.activation) << "\n\t\t";
  std::cout << "pre_norm: " << static_cast<int>(llm_params_.ff_params.pre_norm)
            << "\n\t\t";
  std::cout << "post_norm: "
            << static_cast<int>(llm_params_.ff_params.post_norm) << "\n\t";
  std::cout << "final_norm: " << static_cast<int>(llm_params_.final_norm)
            << "\n\t";
  std::cout << "final_proj_params:" << "\n\t\t";
  std::cout << "no_bias: " << llm_params_.final_proj_params.no_bias << "\n\t";
  std::cout << "enable_kv_cache: " << llm_params_.enable_kv_cache << "\n\t";
  std::cout << "enable_dynamic_shape: " << llm_params_.enable_dynamic_shape
            << "\n\t";
  std::cout << "cache_dir: " << llm_params_.cache_dir << "\n";
  std::cout << "Weights Info:\n";
  for (const auto &sa : llm_weights_.sas) {
    std::cout << "\tSelf Attention:\n";
    PrintNormWeightInfo(2, "pre_norm_weight", sa.pre_norm_weight);
    PrintTensorInfo(2, "k_weight", sa.k_weight);
    PrintTensorInfo(2, "k_bias", sa.k_bias);
    PrintTensorInfo(2, "q_weight", sa.q_weight);
    PrintTensorInfo(2, "q_bias", sa.q_bias);
    PrintTensorInfo(2, "v_weight", sa.v_weight);
    PrintTensorInfo(2, "v_bias", sa.v_bias);
    PrintTensorInfo(2, "per_dim_scale", sa.per_dim_scale);
    PrintTensorInfo(2, "post_proj_weight", sa.post_proj_weight);
    PrintTensorInfo(2, "post_proj_bias", sa.post_proj_bias);
    PrintNormWeightInfo(2, "post_norm_weight", sa.post_norm_weight);
  }

  for (const auto &ff : llm_weights_.ffs) {
    std::cout << "\tFeed Forward:\n";
    PrintNormWeightInfo(2, "pre_norm_weight", ff.pre_norm_weight);
    PrintTensorInfo(2, "layer_1_weight", ff.layer_1_weight);
    PrintTensorInfo(2, "layer_1_bias", ff.layer_1_bias);
    PrintTensorInfo(2, "layer_1_gate_weight", ff.layer_1_gate_weight);
    PrintTensorInfo(2, "layer_1_gate_bias", ff.layer_1_gate_bias);
    PrintTensorInfo(2, "layer_2_weight", ff.layer_2_weight);
    PrintTensorInfo(2, "layer_2_bias", ff.layer_2_bias);
    PrintNormWeightInfo(2, "post_norm_weight", ff.post_norm_weight);
  }
  PrintNormWeightInfo(1, "final_norm_weight", llm_weights_.final_norm_weight);
  PrintTensorInfo(1, "softmax_linear", llm_weights_.softmax_linear);
  PrintTensorInfo(1, "softmax_bias", llm_weights_.softmax_bias);
  PrintTensorInfo(1, "token_embedding", llm_weights_.token_embedding);
  std::cout << "\tCustom Weights:\n";
  for (const auto &entry : llm_weights_.custom_weights) {
    PrintTensorInfo(2, entry.first, entry.second);
  }
#endif
}

absl::StatusOr<std::unique_ptr<Llm>> Llm::CreateLlm(
    std::unique_ptr<LlmWeightsLoader> weight_loader,
    std::unique_ptr<RuntimeConfigs> runtime_configs) {
    auto llm = std::make_unique<Llm>();
    llm->llm_params_ = weight_loader->llm_params();
    ASSIGN_OR_RETURN(llm->llm_weights_, weight_loader->LoadWeights());

    llm->PrintParamsAndWeights();

    return llm;
}

std::optional<Llm::NormWeights> Llm::XNNNormWeightsToHalideNormWeights(
    std::optional<LlmWeights::NormWeights> &in) {
    if (in) {
        if (in->index() == 0) {
            RMSNormWeights halide_rms_norm;
            TensorToHalideBuffer(std::get<0>(*in).norm_weight,
                                 &halide_rms_norm.norm_weight);
            return halide_rms_norm;
        } else if (in->index() == 1) {
            LayerNormWeights halide_layer_norm;
            auto &layer_norm_weights = std::get<1>(*in);
            halide_layer_norm.epsilon = layer_norm_weights.epsilon;
            TensorToHalideBuffer(layer_norm_weights.gamma, &halide_layer_norm.gamma);
            TensorToHalideBuffer(layer_norm_weights.beta, &halide_layer_norm.beta);
            return halide_layer_norm;
        }
    }
    return std::nullopt;
}

absl::Status Llm::Reset() {
    // TODO - b/325325100: avoid clear().
    prev_ids_.clear();
    last_kv_cache_start_ = 0;
    attention_mask_values_ = Halide::Runtime::Buffer<>();
    position_embedding_values_ = Halide::Runtime::Buffer<>();
    segment_pos_values_ = Halide::Runtime::Buffer<float>(llm_params_.head_dim_H,
                                                         llm_params_.seq_size_T);
    // TODO: This will be potentially large. Though probably not onerously so
    // compared to weights. Halide currently doesn't support sparse buffers, but
    // it might be possible to use extern calls to get slices of the cache,
    // which might allow using a non-contiguous representation.
    kv_cache_.clear();
    kv_cache_.resize(llm_params_.num_transformer_M);
    for (auto &entry : kv_cache_) {
        auto k_cache = Halide::Runtime::Buffer<float>(
            llm_params_.head_dim_H,
            1,  // llm_params_.model_dim_D / llm_params_.head_dim_H,
            llm_params_.seq_size_T, llm_params_.batch_size_B);
        k_cache.fill(0.0f);
        entry.k_cache = k_cache;
        auto v_cache = Halide::Runtime::Buffer<float>(
            llm_params_.head_dim_H,
            1,  // llm_params_.model_dim_D / llm_params_.head_dim_H,
            llm_params_.seq_size_T, llm_params_.batch_size_B);
        v_cache.fill(0.0f);
        entry.v_cache = v_cache;
    }

    ffs_.reserve(llm_weights_.ffs.size());
    for (auto &w : llm_weights_.ffs) {
        FeedForwardWeights halide_weights;
        halide_weights.pre_norm_weight =
            XNNNormWeightsToHalideNormWeights(w.pre_norm_weight);
        TensorToHalideBuffer(w.layer_1_weight, &halide_weights.layer_1_weight,
                             &halide_weights.layer_1_scale);
        TensorToHalideBuffer(w.layer_1_bias, &halide_weights.layer_1_bias);
        TensorToHalideBuffer(w.layer_1_gate_weight,
                             &halide_weights.layer_1_gate_weight,
                             &halide_weights.layer_1_gate_scale);
        TensorToHalideBuffer(w.layer_1_gate_bias,
                             &halide_weights.layer_1_gate_bias);
        TensorToHalideBuffer(w.layer_2_weight, &halide_weights.layer_2_weight,
                             &halide_weights.layer_2_scale);
        TensorToHalideBuffer(w.layer_2_bias, &halide_weights.layer_2_bias);
        halide_weights.post_norm_weight =
            XNNNormWeightsToHalideNormWeights(w.post_norm_weight);
        ffs_.emplace_back(halide_weights);
    }

    sas_.reserve(llm_weights_.sas.size());
    for (auto &sa : llm_weights_.sas) {
        SelfAttentionWeights halide_weights;
        halide_weights.pre_norm_weight =
            XNNNormWeightsToHalideNormWeights(sa.pre_norm_weight);
        TensorToHalideBuffer(sa.k_weight, &halide_weights.k_weight,
                             &halide_weights.k_scale);
        TensorToHalideBuffer(sa.k_bias, &halide_weights.k_bias);
        TensorToHalideBuffer(sa.q_weight, &halide_weights.q_weight,
                             &halide_weights.q_scale);
        TensorToHalideBuffer(sa.q_bias, &halide_weights.q_bias);
        TensorToHalideBuffer(sa.v_weight, &halide_weights.v_weight,
                             &halide_weights.v_scale);
        TensorToHalideBuffer(sa.v_bias, &halide_weights.v_bias);
        TensorToHalideBuffer(sa.per_dim_scale, &halide_weights.per_dim_scale);
        TensorToHalideBuffer(sa.post_proj_weight, &halide_weights.post_proj_weight,
                             &halide_weights.post_proj_scale);
        TensorToHalideBuffer(sa.post_proj_bias, &halide_weights.post_proj_bias);
        halide_weights.post_norm_weight =
            XNNNormWeightsToHalideNormWeights(sa.post_norm_weight);
        sas_.emplace_back(halide_weights);
    }

    final_norm_weight_ =
        XNNNormWeightsToHalideNormWeights(llm_weights_.final_norm_weight);
    TensorToHalideBuffer(llm_weights_.softmax_linear, &softmax_linear_weights_,
                         &softmax_linear_scale_);

    // TODO: fix when Tensor is filled out
    // if (llm_weights_.softmax_linear && llm_weights_.softmax_linear->datatype != xnn_datatype_fp32) {
    //     ASSIGN_OR_RETURN(softmax_linear_f32_tensor_,
    //                      llm_weights_.softmax_linear->ConvertToF32());
    //     TensorToHalideBuffer(*softmax_linear_f32_tensor_, &softmax_linear_f32_);
    // }

    TensorToHalideBuffer(llm_weights_.softmax_bias, &softmax_bias_);
    if (llm_weights_.token_embedding) {
        // std::cout << "Model has token embedding.\n";
        Halide::Runtime::Buffer<> temp;
        TensorToHalideBuffer(llm_weights_.token_embedding, &temp);
        token_embedding_ = temp;
    }

    return absl::OkStatus();
}

absl::Status Llm::InitAttentionMaskValues(size_t process_seq_len) {
    const auto &seq_size = llm_params_.seq_size_T;
    constexpr float neg_value = 0.5 * std::numeric_limits<float>::lowest();
    Halide::Runtime::Buffer<float> attention_mask_values(seq_size, seq_size);
    // TODO: Could be sped up as a Halide kernel.
    switch (llm_params_.model_type) {
    case LlmParams::ModelType::PREFIX: {
        std::cout << "InitAttentionMaskValues prefix\n";
        // RET_CHECK_LE(process_seq_len, seq_size);
        // Prefix full attention for all tokens within input ids size(input),
        // and causal attention mask for all following tokens.
        for (int i = 0; i < seq_size; ++i) {
            for (int j = 0; j < seq_size; ++j) {
                attention_mask_values(j, i) =
                    (j <= i || std::max(j, i) < process_seq_len) ? 0.0f : neg_value;
            }
        }
        break;
    }
    case LlmParams::ModelType::CAUSAL: {
        // std::cout << "InitAttentionMaskValues causal\n";
        for (int i = 0; i < seq_size; ++i) {
            for (int j = 0; j < seq_size; ++j) {
                attention_mask_values(j, i) = (j <= i) ? 0 : neg_value;
            }
        }
        break;
    }
    default: {
        return absl::InvalidArgumentError(
            absl::StrCat("Unsupported model type: ", llm_params_.model_type));
    }
    }
#if DUMP_INFO_TO_STDOUT
    std::cout << "AttentionMaskValues dims [" << seq_size << ", " << seq_size
              << "]\n";
    std::cout << "AttentionMaskValues[0, 0]: " << attention_mask_values(0, 0)
              << "\n";
    std::cout << "AttentionMaskValues[" << (seq_size - 1)
              << ", 0]: " << attention_mask_values(seq_size - 1, 0) << "\n";
    std::cout << "AttentionMaskValues[0, 1]: " << attention_mask_values(0, 1)
              << "\n";
    std::cout << "AttentionMaskValues[" << (seq_size - 1)
              << ", 1]: " << attention_mask_values(seq_size - 1, 1) << "\n";
    std::cout << "AttentionMaskValues[0, " << (seq_size - 1)
              << "]: " << attention_mask_values(0, seq_size - 1) << "\n";
    std::cout << "AttentionMaskValues[" << (seq_size - 1) << ", "
              << (seq_size - 1)
              << "]: " << attention_mask_values(seq_size - 1, seq_size - 1)
              << "\n";
#endif
    attention_mask_values_ = attention_mask_values;
    return absl::OkStatus();
}

Halide::Runtime::Buffer<> Llm::AllocateSeqBuffer(int current_seq_size) {
    int seq_len = llm_params_.enable_dynamic_shape ? current_seq_size : llm_params_.seq_size_T;
    auto result = Halide::Runtime::Buffer<float>(llm_params_.model_dim_D, seq_len,
                                                 llm_params_.batch_size_B);
    result.fill(0.0f);
    return result;
}

// TODO: Rewrite this whole operation in Halide.
absl::Status Llm::UpdateInput(const std::vector<int> &input_ids) {
    // At present prev_ids_ is always empty at entry, but it seems the
    // design is intended to support some sort of incremental operation.
    // RET_CHECK_LE(input_ids.size() + prev_ids_.size(), llm_params_.seq_size_T);
    if (token_embedding_) {
        PrintBuffer("token_embedding_", *token_embedding_);
    }
    PrintInFloatBuffer("softmax_linear_f32_", softmax_linear_f32_);
    auto token_embedding =
        token_embedding_ ? *token_embedding_ : softmax_linear_f32_;
    // RET_assert(token_embedding);
    // RET_assert(token_embedding.dim(1).extent() == llm_params_.voc_size_V)
    //     << "Token embedding dim(1) size does not match voc_size_V  parameter.";
    // RET_assert(token_embedding.dim(0).extent() == llm_params_.model_dim_D)
    //     << "Token embedding dim(0) size does not match model_dim_D parameter.";
    // // TODO: Support conversion.
    // RET_assert(token_embedding.type().code == halide_type_float &&
    //            token_embedding.type().bits == 32)
    //     << "Token embedding is not 32-bit float.";
    Halide::Runtime::Buffer<float> float_token_embedding = token_embedding;
    Halide::Runtime::Buffer<float> float_input = *transformer_input_;
    size_t base_id = prev_ids_.size();
    for (size_t batch = 0; batch < llm_params_.batch_size_B; ++batch) {
        for (size_t id = 0; id < input_ids.size(); id++) {
            memcpy(&float_input(0, static_cast<int32_t>(base_id + id), batch),
                   &float_token_embedding(0, input_ids[id]),
                   llm_params_.model_dim_D * sizeof(float));
        }
    }
    PrintInFloatBuffer("float_token_embedding", float_token_embedding);
    PrintInFloatBuffer("transformer_input_", *transformer_input_);
    prev_ids_.insert(prev_ids_.end(), input_ids.begin(), input_ids.end());
    // prev_id.size - 1 is the output.
    return absl::OkStatus();
}

absl::Status Llm::InitInputTokens(const std::vector<int> &input_ids) {
    RETURN_IF_ERROR(Reset());

    RETURN_IF_ERROR(InitAttentionMaskValues(input_ids.size()));

    if (!llm_params_.skip_absolute_positional_embeddings) {
        std::cout << "Initing pos_embedding.\n";
        pos_embedding_ = Halide::Runtime::Buffer<float>(
            static_cast<int32_t>(llm_params_.model_dim_D),
            static_cast<int32_t>(llm_params_.seq_size_T));
        int32_t input_length;
        switch (llm_params_.model_type) {
        case LlmParams::ModelType::PREFIX:
            input_length = input_ids.size();
            break;
        case LlmParams::ModelType::CAUSAL:
            input_length = prev_ids_.size();
            break;
        default:
            return absl::InvalidArgumentError(
                absl::StrCat("Unsupported model type: ", llm_params_.model_type));
        }
        RETURN_IF_ERROR(StatusFromHalide(position_embedding(
            input_length, llm_params_.seq_size_T, llm_params_.model_dim_D, 1.0f,
            10000.0f, pos_embedding_)));
    }

    RETURN_IF_ERROR(StatusFromHalide(rope_values(segment_pos_values_)));
    PrintInFloatBuffer2D("segment_pos_values_", segment_pos_values_);

    // Prepare input from ids and token embedding.
    // TODO: Do we need to resize input here?
    if (!transformer_input_) {
        transformer_input_ =
            AllocateSeqBuffer(static_cast<int32_t>(llm_params_.seq_size_T));
    }

    RETURN_IF_ERROR(UpdateInput(input_ids));

    if (llm_params_.enable_kv_cache) {
        RETURN_IF_ERROR(GetNextToken(&saved_token_));
        // std::cout << "Saved token is: " << saved_token_[0] << "\n";
    }

    return absl::OkStatus();
}

absl::Status Llm::GetNextToken(std::vector<int> *output_ids) {
    if (!saved_token_.empty()) {
        *output_ids = std::move(saved_token_);
        saved_token_.clear();
        return absl::OkStatus();
    }

    if (prev_ids_.size() >= llm_params_.seq_size_T - 1) {
        return absl::OutOfRangeError(
            absl::StrCat("Hit max sequence length ", llm_params_.seq_size_T));
    }

    // PrefixDecodeLlm::GetNExtToken.
    RETURN_IF_ERROR(Run());

    // RET_assert(logits_output_);
    // ABSL_DCHECK_EQ(logits_output_->number_of_elements(), llm_params_.voc_size_V);

    // TODO: look at either making sampling take a pointer or reimplement to avoid
    // copy here.
    std::vector<size_t> tensor_dims;
    for (size_t i = logits_output_->dimensions(); i > 0; i--) {
        tensor_dims.push_back(logits_output_->dim(i - 1).extent());
    }

    // TODO: uncomment when Tensor is implemented, or replace with Buffer
    // Tensor temp_logits(tensor_dims);
    // RETURN_IF_ERROR(temp_logits.LoadFromBuffer(logits_output_->data()));
    // if (sampler_ == nullptr) {
    //     ASSIGN_OR_RETURN(sampler_,
    //                      Sampler::Create(Sampler::Type::kGreedy,
    //                                      /*top_k=*/0,
    //                                      /*top_p=*/0.0,
    //                                      /*top_temperature=*/0.0,
    //                                      /*seed=*/0));
    // }
    // ASSIGN_OR_RETURN(*output_ids, sampler_->Sample(temp_logits));
    // RET_CHECK_EQ((output_ids != nullptr) && output_ids->size(), 1);

#if DUMP_INFO_TO_STDOUT
    std::cout << "Output ID size is " << output_ids->size() << " is "
              << output_ids->at(0) << "\n";
#endif

    return UpdateInput(*output_ids);
}

absl::Status Llm::RunStack(Llm::TempBuffers &buffers) {
    int decode_step = prev_ids_.size();
    int run_extent = decode_step - last_kv_cache_start_;

#if DUMP_INFO_TO_STDOUT
    if (decode_step == 6) exit(0);
    std::cout << "Llm::RunStack: Decode step " << decode_step << " run_extent "
              << run_extent << " llm_params.enable_dynamic_shape "
              << llm_params_.enable_dynamic_shape << "\n";
    {
        // std::cout << "Llm::RunStack transformer_input_" << *transformer_input_
        //           << "\n";
        Halide::Runtime::Buffer<float> temp_buf_out = *transformer_input_;
        DumpFloatBuffer("transformer_input_", *transformer_input_, 16,
                        decode_step + 1);
#if 0
  PrintBuffer("transformer_input_", temp_buf_out);
  int start = llm_params_.enable_kv_cache ? prev_ids.size() : 0;
  int end = llm_params_.enable_kv_cache ? 1 : prev_ids.size();
  for (int tok = start; tok < end; tok++) {
    std::cout << "Llm::GetNextToken start transformer_input_ tokens " << tok << "\n";
    dump_segpos(&temp_buf_out(0, tok, 0), 32);
    std::cout << "Llm::GetNextToken end transformer_input_ tokens " << tok << "\n";
  }
#endif
    }
#endif

    if (llm_params_.enable_kv_cache) {
        buffers.FocusSeqDimCrop(last_kv_cache_start_, run_extent);
    } else {
        buffers.FocusSeqDimCrop(0, prev_ids_.size());
    }

    RETURN_IF_ERROR(StatusFromHalide(hamal::preprocessor(
        *transformer_input_, buffers.StartInput())));

    DumpFloatBuffer("start_input", buffers.StartInput(), 16, 2);

    if (llm_params_.enable_kv_cache) {
        // std::cout << "Compute attention_slice: " <<
        //   last_kv_cache_start_ << " run_extent: " << run_extent << "\n";
        Halide::Runtime::Buffer<> attention_slice =  // attention_mask_values_;
            attention_mask_values_.cropped(1, last_kv_cache_start_,
                                           run_extent);
        PrintBuffer("attention_slice", attention_slice);
        for (int i = 0; i < llm_params_.num_transformer_M; i++) {
            auto &sas(sas_[i]);
            auto &ffs(ffs_[i]);

            // std::cout << "Compute output last_kv_cache_start_: "
            //           << last_kv_cache_start_ << " run_extent: " << run_extent
            //           << "\n";
            auto key_slice =
                kv_cache_[i].k_cache.cropped(2, last_kv_cache_start_, run_extent);
            auto value_slice =
                kv_cache_[i].v_cache.cropped(2, last_kv_cache_start_, run_extent);

#if DUMP_INFO_TO_STDOUT
            std::cout << "Compute output step " << i << "\n";
#endif
            DumpFloatBuffer("Compute enable_kv_cache input", buffers.CurrentInput(), 16);
            // DumpFloatBuffer("Compute output k_cache", kv_cache_[i].k_cache, 16);
            // DumpFloatBuffer("Compute output v_cache", kv_cache_[i].v_cache, 16);
            DumpFloatBuffer("Compute output attention_slice", attention_slice, 16);

            RETURN_IF_ERROR(
                StatusFromHalide(hamal::transformer_kv_update_cache(
                    buffers.CurrentInput(), segment_pos_values_, attention_slice,
                    std::get<0>(*(sas.pre_norm_weight)).norm_weight, sas.k_weight,
                    sas.k_scale, sas.q_weight, sas.q_scale, sas.v_weight, sas.v_scale,
                    sas.post_proj_weight, sas.post_proj_scale, key_slice,
                    value_slice)));
#if DUMP_INFO_TO_STDOUT
            std::cout << "Done with transformer_kv_update_cache " << i << "\n";
#endif
            RETURN_IF_ERROR(
                StatusFromHalide(hamal::transformer_kv_use_cache(
                    buffers.CurrentInput(), segment_pos_values_, attention_slice,
                    std::get<0>(*(sas.pre_norm_weight)).norm_weight, sas.k_weight,
                    sas.k_scale, sas.q_weight, sas.q_scale, sas.v_weight, sas.v_scale,
                    sas.post_proj_weight, sas.post_proj_scale,
                    std::get<0>(*(ffs.pre_norm_weight)).norm_weight, ffs.layer_1_weight,
                    ffs.layer_1_scale, ffs.layer_1_gate_weight,
                    ffs.layer_1_gate_scale, ffs.layer_2_weight, ffs.layer_2_scale,
                    kv_cache_[i].k_cache, kv_cache_[i].v_cache,
                    buffers.CurrentOutput())));

            DumpFloatBuffer("Compute output output", buffers.CurrentOutput(), 16);
            buffers.Swap();
        }
        last_kv_cache_start_ += run_extent;
    } else {
        for (int i = 0; i < llm_params_.num_transformer_M; i++) {
            auto &sas(sas_[i]);
            auto &ffs(ffs_[i]);

#if DUMP_INFO_TO_STDOUT
            std::cout << "Compute output step " << i << "\n";
#endif
            DumpFloatBuffer("Compute !enable_kv_cache input", buffers.CurrentInput(), 16);
            DumpFloatBuffer("Compute output attention_slice", attention_mask_values_, 16);
            // PrintBuffer("current output", buffers.CurrentOutput());

            RETURN_IF_ERROR(
                StatusFromHalide(hamal::transformer_no_kv_cache(
                    buffers.CurrentInput(), segment_pos_values_,
                    attention_mask_values_,
                    std::get<0>(*(sas.pre_norm_weight)).norm_weight, sas.k_weight,
                    sas.k_scale, sas.q_weight, sas.q_scale, sas.v_weight, sas.v_scale,
                    sas.post_proj_weight, sas.post_proj_scale,
                    std::get<0>(*(ffs.pre_norm_weight)).norm_weight, ffs.layer_1_weight,
                    ffs.layer_1_scale, ffs.layer_1_gate_weight,
                    ffs.layer_1_gate_scale, ffs.layer_2_weight, ffs.layer_2_scale,
                    buffers.CurrentOutput())));

            DumpFloatBuffer("Compute output output", buffers.CurrentOutput(), 16);
            buffers.Swap();
        }
    }

    PrintInFloatBuffer("current_output_ after transformer stack",
                       buffers.CurrentOutput());
#if 1
#if DUMP_INFO_TO_STDOUT
    Halide::Runtime::Buffer<float> temp_buf_out = buffers.CurrentInput();
    std::cout << "Start of dump for transformer stack output:\n";
    dump_segpos(temp_buf_out.data(), 2048 * 3);
    std::cout << "End of dump for transformer stack output\n";
#endif
#endif

    // TODO: can free current_output_ here as not currently reused.
    logits_output_ = Halide::Runtime::Buffer<float>(llm_params_.voc_size_V, 1,
                                                    llm_params_.batch_size_B);
    // Only compute logits for the last token.

    PrintBuffer("logits current input", buffers.CurrentInput());
    PrintBuffer("logits current output", buffers.CurrentOutput());

    logits_output_->set_min(0, buffers.CurrentInput().dim(1).max(), 0);
    // Postprocess
    RETURN_IF_ERROR(StatusFromHalide(hamal::postprocessor(
        buffers.CurrentInput(), std::get<0>(*final_norm_weight_).norm_weight,
        softmax_linear_weights_, softmax_linear_scale_, *logits_output_)));

#if DUMP_INFO_TO_STDOUT
    Halide::Runtime::Buffer<float> temp_buf = *logits_output_;
    std::cout << "Start of dump for logits output:\n";
    dump_segpos(temp_buf.data(), 2048 * 3);
    std::cout << "End of dump for logits output\n";
#endif  // DUMP_INFO_TO_STDOUT

    return absl::OkStatus();
}

absl::StatusOr<Llm::TempBuffers> Llm::AllocateTempBuffers(int extent) {
    TempBuffers result;
    result.initial_input_full = AllocateSeqBuffer(extent);
    result.buffers_full[0] = AllocateSeqBuffer(extent);
    result.buffers_full[1] = AllocateSeqBuffer(extent);

    result.FocusSeqDimCrop(0, extent);

    return result;
}

absl::Status Llm::Run() {
    // Weights cache operations?
    // KV cache?

    TempBuffers buffers;
    ASSIGN_OR_RETURN(buffers,
                     AllocateTempBuffers(transformer_input_->dim(1).extent()));

    RETURN_IF_ERROR(RunStack(buffers));

    return absl::OkStatus();
}

}  // namespace hamal
