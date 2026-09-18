#ifndef HALIDE_APPS_HALLMARK_LLM_WEIGHTS_H_
#define HALIDE_APPS_HALLMARK_LLM_WEIGHTS_H_

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "HalideBuffer.h"

namespace hallmark {

// Provides access to data tied to an underlying resource. The resource may be
// released when this object is destroyed.
class DataHolder {
public:
    virtual ~DataHolder() = default;
};

// If dim_scale >= 0, then `weights` should be scaled by that dimension.
// Otherwise, scale is an empty (unallocated) Buffer.
struct ScaledTensor {
    Halide::Runtime::Buffer<> weights, scale;
    int dim_scale = -1;
};

struct RMSNormWeights {
    ScaledTensor norm_weight;
};

struct LayerNormWeights {
    float epsilon = 1e-5;
    ScaledTensor gamma;
    ScaledTensor beta;
};

struct LlmWeights {
    using NormWeights = std::variant<RMSNormWeights, LayerNormWeights>;

    struct SelfAttentionWeights {
        std::optional<NormWeights> pre_norm_weight;

        ScaledTensor k_weight;
        ScaledTensor k_bias;
        ScaledTensor q_weight;
        ScaledTensor q_bias;
        ScaledTensor v_weight;
        ScaledTensor v_bias;
        ScaledTensor per_dim_scale;
        ScaledTensor post_proj_weight;
        ScaledTensor post_proj_bias;

        std::optional<NormWeights> post_norm_weight;
    };

    struct FeedForwardWeights {
        std::optional<NormWeights> pre_norm_weight;
        ScaledTensor layer_1_weight;
        ScaledTensor layer_1_bias;
        ScaledTensor layer_1_gate_weight;
        ScaledTensor layer_1_gate_bias;
        ScaledTensor layer_2_weight;
        ScaledTensor layer_2_bias;
        std::optional<NormWeights> post_norm_weight;
    };

    std::vector<FeedForwardWeights> ffs;
    std::vector<SelfAttentionWeights> sas;
    std::optional<NormWeights> final_norm_weight;
    ScaledTensor softmax_linear;
    ScaledTensor softmax_bias;

    // Usually same as softmax_linear, but some models use different
    // softmax_linear v.s. embedding table.
    ScaledTensor token_embedding;

    // TODO: a bit of an ugly hack here; if the weights are loaded from
    // a memory-mapped file, this is a shared_ptr to ensure that the mapping
    // remains valid for the life of this instance.
    std::shared_ptr<DataHolder> data_holder;
};

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_LLM_WEIGHTS_H_
