#include "contrib/llm_params.h"

#include "contrib/memory_mapped_file.h"
#include "contrib/status_helpers.h"
#include "contrib/llm_params.pb.h"
#include "contrib/transformer_params.pb.h"
// macOS system headers #define this value in syslimits.h
#undef ARG_MAX
#include "contrib/tflite_schema_generated.h"

namespace hallmark {

namespace {

using odml::infra::proto::LlmParameters;
using odml::infra::proto::TransformerParameters;

const ::tflite::Metadata *FindMetadata(const ::tflite::Model *tflite_model,
                                       std::string name) {
    if (tflite_model->metadata() == nullptr) {
        return nullptr;
    }

    for (const auto *metadata : *tflite_model->metadata()) {
        if (name == metadata->name()->c_str()) {
            return metadata;
        }
    }
    return nullptr;
}

LlmParams::Norm TransformerParametersProtoNormTypeToLlmParamsNormType(
    TransformerParameters::Norm norm_type) {
    switch (norm_type) {
    case TransformerParameters::NORM_UNSPECIFIED:
        ABSL_LOG(DFATAL) << "Unspecified norm type.";
        return LlmParams::Norm::UNSPECIFIED;
    case TransformerParameters::NO_NORM:
        return LlmParams::Norm::NO_NORM;
    case TransformerParameters::RMS_NORM:
        return LlmParams::Norm::RMS_NORM;
    case TransformerParameters::LAYER_NORM:
        return LlmParams::Norm::LAYER_NORM;
    default:
        ABSL_LOG(DFATAL) << "Unknown norm type: " << norm_type;
    }
    return LlmParams::Norm::UNSPECIFIED;
}

LlmParams FromLLMParametersProto(const LlmParameters &llm_params) {
    const auto &transformer_params = llm_params.transformer_parameters();
    LlmParams params = {
        .num_transformer_M = static_cast<size_t>(transformer_params.num_stacks()),
        .batch_size_B = static_cast<size_t>(transformer_params.batch_size()),
        .seq_size_T = static_cast<size_t>(transformer_params.max_seq_length()),
        .model_dim_D = static_cast<size_t>(transformer_params.embedding_dim()),
        .hidden_dim_HD =
            static_cast<size_t>(transformer_params.hidden_dimension()),
        .head_dim_H = static_cast<size_t>(transformer_params.head_dimension()),
        .n_heads_N = static_cast<size_t>(transformer_params.num_heads()),
        .voc_size_V = static_cast<size_t>(llm_params.vocab_size()),

        .num_kv_heads =
            static_cast<size_t>(transformer_params.num_kv_heads() == 0 ? transformer_params.num_heads() : transformer_params.num_kv_heads()),
        .enable_kv_cache = false,
        .enable_dynamic_shape = false};
    switch (
        transformer_params.self_attention_parameters().attention_mask_type()) {
    case TransformerParameters::UNSPECIFIED:
        ABSL_LOG(DFATAL) << "Unspecified attention_mask_type, assuming causal";
        params.model_type = LlmParams::ModelType::UNSPECIFIED;
        break;
    case TransformerParameters::CAUSAL:
        params.model_type = LlmParams::ModelType::CAUSAL;
        break;
    case TransformerParameters::PREFIX:
        params.model_type = LlmParams::ModelType::PREFIX;
        break;
    default:
        ABSL_LOG(DFATAL) << "Unknown attention_mask_type: "
                         << transformer_params.self_attention_parameters()
                                .attention_mask_type();
    }
    params.ff_params = LlmParams::FeedForwardParams{
        .no_bias = transformer_params.feed_forward_parameters().no_bias(),
    };
    params.final_proj_params = LlmParams::FinalProjectParams{
        .no_bias = transformer_params.final_project_parameters().no_bias(),
    };
    switch (transformer_params.feed_forward_parameters().activation()) {
    case TransformerParameters::ACTIVATION_UNSPECIFIED:
        ABSL_LOG(DFATAL) << "Unspecified feed_forward_parameters.activation.";
        params.ff_params.activation = LlmParams::Activation::UNSPECIFIED;
        break;
    case TransformerParameters::GELU:
        params.ff_params.activation = LlmParams::Activation::GELU;
        break;
    case TransformerParameters::SILU:
        params.ff_params.activation = LlmParams::Activation::SILU;
        break;
    case TransformerParameters::RELU:
        params.ff_params.activation = LlmParams::Activation::RELU;
        break;
    default:
        ABSL_LOG(DFATAL)
            << "Unknown feed_forward_parameters.activation: "
            << transformer_params.feed_forward_parameters().activation();
    }
    params.sa_params.qkv_no_bias =
        transformer_params.self_attention_parameters().qkv_no_bias();
    params.sa_params.post_proj_no_bias =
        transformer_params.self_attention_parameters().post_proj_no_bias();
    params.sa_params.pre_norm =
        TransformerParametersProtoNormTypeToLlmParamsNormType(
            transformer_params.pre_norm());
    params.sa_params.post_norm =
        TransformerParametersProtoNormTypeToLlmParamsNormType(
            transformer_params.post_norm());
    params.sa_params.soft_cap_value =
        transformer_params.self_attention_parameters().soft_cap_value();
    params.ff_params.pre_norm =
        TransformerParametersProtoNormTypeToLlmParamsNormType(
            transformer_params.feed_forward_parameters().pre_norm());
    params.ff_params.post_norm =
        TransformerParametersProtoNormTypeToLlmParamsNormType(
            transformer_params.feed_forward_parameters().post_norm());
    params.final_norm = TransformerParametersProtoNormTypeToLlmParamsNormType(
        transformer_params.final_norm());
    params.skip_absolute_positional_embeddings =
        transformer_params.skip_absolute_positional_embeddings();
    if (transformer_params.self_attention_parameters()
            .has_attention_scale_type()) {
        switch (
            transformer_params.self_attention_parameters().attention_scale_type()) {
        case TransformerParameters::SCALE_TYPE_UNSPECIFIED:
            ABSL_LOG(DFATAL) << "Unspecified attention_scale_type.";
            params.sa_params.attention_scale_type =
                LlmParams::AttentionScaleType::UNSPECIFIED;
            break;
        case TransformerParameters::SCALE_TYPE_PER_DIM_SCALE:
            params.sa_params.attention_scale_type =
                LlmParams::AttentionScaleType::PER_DIM_SCALE;
            break;
        case TransformerParameters::SCALE_TYPE_INV_SQRT_HEAD_DIM:
            params.sa_params.attention_scale_type =
                LlmParams::AttentionScaleType::INV_SQRT_HEAD_DIM;
            break;
        default:
            ABSL_LOG(DFATAL) << "Unknown attention_scale_type: "
                             << transformer_params.self_attention_parameters()
                                    .attention_scale_type();
        }
    } else {
        if (transformer_params.num_kv_heads() == 0 ||
            transformer_params.num_heads() == transformer_params.num_kv_heads()) {
            // If MHA, PER_DIM_SCALE is used.
            params.sa_params.attention_scale_type =
                LlmParams::AttentionScaleType::PER_DIM_SCALE;
        } else {
            // If MQA or GQA, INV_SQRT_HEAD_DIM is used.
            params.sa_params.attention_scale_type =
                LlmParams::AttentionScaleType::INV_SQRT_HEAD_DIM;
        }
    }

    return params;
}

}  // namespace

absl::StatusOr<LlmParams> LoadLlmParams(absl::string_view tflite_path) {
    MemoryMappedFile file(tflite_path);
    if (!file.valid()) {
        return absl::InvalidArgumentError("Could not open file for llm_params");
    }

    const ::tflite::Model *tflite_model = ::tflite::GetModel(file.data());
    const auto *metadata =
        FindMetadata(tflite_model, "odml.infra.proto.LlmParameters");
    if (!metadata) {
        return absl::InvalidArgumentError("No metadata found in model");
    }

    const ::tflite::Buffer *buffer =
        tflite_model->buffers()->Get(metadata->buffer());
    const void *base = (const char *)file.data() + buffer->offset();
    const size_t len = buffer->size();

    LlmParameters llm_parameters;
    RET_CHECK(llm_parameters.ParseFromArray(base, len));
    return FromLLMParametersProto(llm_parameters);
}

}  // namespace hallmark
