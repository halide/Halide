#ifndef HALIDE_APPS_HALLMARK_LLM_PARAMS_H_
#define HALIDE_APPS_HALLMARK_LLM_PARAMS_H_

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace hallmark {

struct LlmParams {
    size_t num_transformer_M = 0;
    size_t batch_size_B = 0;
    size_t seq_size_T = 0;
    size_t model_dim_D = 0;
    size_t hidden_dim_HD = 0;
    size_t head_dim_H = 0;
    size_t n_heads_N = 0;
    size_t voc_size_V = 0;

    // Number of kv heads. In case of Multi-Head-Attention (MHA), num_kv_heads is
    // the same as n_heads_N, which is number of query heads; In case of
    // Multi-Query-Attention (MQA), key and value have one head; otherwise, this
    // specifies the number of heads for key and value, and
    // Grouped-Query-Attention (GQA) will be used. See
    // https://arxiv.org/pdf/2305.13245.pdf for details.
    size_t num_kv_heads = 0;

    // Meant to be a mapping of pax LanguageModelType. This will affect e.g.
    // attention mask shape.
    enum class ModelType {
        UNSPECIFIED = 0,
        // Attention mask for input are prefixed to be bidirectional.
        PREFIX = 1,
        // Attention mask are forward only.
        CAUSAL = 2,
    } model_type = ModelType::CAUSAL;

    enum class Activation {
        UNSPECIFIED = 0,
        // Gaussian Error Linear Unit.
        GELU = 1,
        // Sigmoid-Weighted Linear Unit.
        SILU = 2,
        // Rectified Linear Unit.
        RELU = 3,
    };

    enum class Norm {
        UNSPECIFIED = 0,
        NO_NORM = 1,
        RMS_NORM = 2,
        LAYER_NORM = 3,
    };

    enum class AttentionScaleType {
        UNSPECIFIED = 0,
        // Per dimension scale, query is scaled by log_2(1 + exp(w)) /
        // sqrt(head_dim) where w is s static weight.
        PER_DIM_SCALE = 1,
        // Query is scaled by 1/sqrt(head_dim).
        INV_SQRT_HEAD_DIM = 2,
    };

    // If false, add absolute positional embeddings.
    bool skip_absolute_positional_embeddings = false;

    struct SelfAttentionParams {
        bool qkv_no_bias = false;
        bool post_proj_no_bias = false;
        Norm pre_norm = Norm::RMS_NORM;
        Norm post_norm = Norm::RMS_NORM;

        // If greater than 0, CapTanh will be applied. Otherwise, no cap will be
        // applied.
        float soft_cap_value = 0.0f;

        // Attention scale type to be applied within the transformer.
        AttentionScaleType attention_scale_type;
    } sa_params;

    struct FeedForwardParams {
        // If `no_bias`, fully connect will degrade to matrix multiply.
        bool no_bias = false;
        Activation activation = Activation::GELU;
        Norm pre_norm = Norm::RMS_NORM;
        Norm post_norm = Norm::RMS_NORM;
    } ff_params;

    Norm final_norm = Norm::RMS_NORM;

    struct FinalProjectParams {
        // If `no_bias`, final fully connect will degrade to matrix multiply.
        bool no_bias = false;
    } final_proj_params;

    /*
     * Parameters below do NOT change the "correctness" of the model, they
     * configure the acceleration of inference.
     */

    bool enable_kv_cache = false;
    // If true, inference engine will optimize tensor shape according to current
    // sequence length to avoid computation waste.
    bool enable_dynamic_shape = false;
};

absl::StatusOr<LlmParams> LoadLlmParams(absl::string_view tflite_path);

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_LLM_PARAMS_H_
