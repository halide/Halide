#ifndef HALIDE_APPS_HAMAL_LLM_H_
#define HALIDE_APPS_HAMAL_LLM_H_

#include <memory>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace hamal {

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

    // If provided, the runtime will prepare cache at the provided directory.
    // Otherwise, cache will be prepared besides the original model.
    std::string cache_dir;
};

// TODO: stub class
struct Tensor {
};

struct RuntimeConfigs {
    // Number of thread used in Halide thread pool.
    size_t num_threads = 4;
};

struct RMSNormWeights {
  std::shared_ptr<Tensor> norm_weight;
};

struct LayerNormWeights {
  float epsilon = 1e-5;
  std::shared_ptr<Tensor> gamma;
  std::shared_ptr<Tensor> beta;
};

// TODO: taken from xnnpack, do we need everything here?
struct LlmWeights {
  using NormWeights = std::variant<RMSNormWeights, LayerNormWeights>;

  struct SelfAttentionWeights {
    std::optional<NormWeights> pre_norm_weight;

    std::shared_ptr<Tensor> k_weight;
    std::shared_ptr<Tensor> k_bias;
    std::shared_ptr<Tensor> q_weight;
    std::shared_ptr<Tensor> q_bias;
    std::shared_ptr<Tensor> v_weight;
    std::shared_ptr<Tensor> v_bias;
    std::shared_ptr<Tensor> per_dim_scale;
    std::shared_ptr<Tensor> post_proj_weight;
    std::shared_ptr<Tensor> post_proj_bias;

    std::optional<NormWeights> post_norm_weight;
  };

  struct FeedForwardWeights {
    std::optional<NormWeights> pre_norm_weight;
    std::shared_ptr<Tensor> layer_1_weight;
    std::shared_ptr<Tensor> layer_1_bias;
    std::shared_ptr<Tensor> layer_1_gate_weight;
    std::shared_ptr<Tensor> layer_1_gate_bias;
    std::shared_ptr<Tensor> layer_2_weight;
    std::shared_ptr<Tensor> layer_2_bias;
    std::optional<NormWeights> post_norm_weight;
  };

  std::vector<FeedForwardWeights> ffs;
  std::vector<SelfAttentionWeights> sas;
  std::optional<NormWeights> final_norm_weight;
  std::shared_ptr<Tensor> softmax_linear;
  std::shared_ptr<Tensor> softmax_bias;

  // Usually same as softmax_linear, but some models use different
  // softmax_linear v.s. embedding table.
  std::shared_ptr<Tensor> token_embedding;
};

// TODO: stub class
class LlmWeightsLoader {
public:
    LlmWeightsLoader() = default;
    virtual ~LlmWeightsLoader() = default;

    virtual absl::StatusOr<LlmWeights> LoadWeights() {
        std::abort();  // TODO
    }

    LlmParams &llm_params() {
        return params_;
    }
    const LlmParams &llm_params() const {
        return params_;
    }
private:
    LlmParams params_;
};

// TODO: stub class
class Sampler {
 public:
  enum class Type { kGreedy, kTopK, kTopP };

  static absl::StatusOr<std::unique_ptr<Sampler>> Create(Type type, int top_k,
                                                         float top_p,
                                                         float temperature,
                                                         int seed) {
    std::abort();
    return absl::UnknownError("TODO");
  }
};


class Llm {
public:
    Llm() = default;
    Llm(Llm &&) = default;
    ~Llm() = default;

    // Create LLM graph using the `DefaultLlmWeightsLoader` to load model from `weights_folder`.
    static absl::StatusOr<std::unique_ptr<Llm>> CreateLlm(
        std::string weights_folder,
        const LlmParams &llm_params,
        std::unique_ptr<RuntimeConfigs> runtime_configs = nullptr);

    static absl::StatusOr<std::unique_ptr<Llm>> CreateLlm(
        std::unique_ptr<LlmWeightsLoader> weight_loader,
        std::unique_ptr<RuntimeConfigs> runtime_configs = nullptr);

    // (Re)Initialize with input token ids. This will reset the cache, mask etc.
    virtual absl::Status InitInputTokens(const std::vector<int> &input_ids);

    // Get the next token id.
    virtual absl::Status GetNextToken(std::vector<int> *output_ids);

    // The size of all tokens, including prompt and generated tokens.
    size_t TotalTokenSize() {
        return prev_ids_.size();
    }

    const LlmParams &GetLlmParams() {
        return llm_params_;
    }

    // These are public only for test/benchmark purposes; don't use it elsewhere.
    Halide::Runtime::Buffer<> AllocateSeqBuffer(int seq_size);
    absl::Status Reset();
    absl::Status InitAttentionMaskValues(size_t process_seq_len);

private:
    struct TempBuffers {
        Halide::Runtime::Buffer<> initial_input_full;
        Halide::Runtime::Buffer<> buffers_full[2];
        Halide::Runtime::Buffer<> initial_input;
        Halide::Runtime::Buffer<> buffers[2];
        bool first{true};
        int current_input{0};

        void FocusSeqDimCrop(int min, int extent) {
            initial_input = initial_input_full.cropped(1, min, extent);
            buffers[0] = buffers_full[0].cropped(1, min, extent);
            buffers[1] = buffers_full[1].cropped(1, min, extent);
        }

        Halide::Runtime::Buffer<> &StartInput() {
            return initial_input;
        }

        Halide::Runtime::Buffer<> &CurrentInput() {
            return first ? initial_input :
                           buffers[current_input];
        }

        Halide::Runtime::Buffer<> &CurrentOutput() {
            return first ? buffers[0] :
                           buffers[current_input ^ 1];
        }

        void Swap() {
            if (first) {
                first = false;
            } else {
                current_input ^= 1;
            }
        }

        void ResetToStart() {
            first = true;
            current_input = 0;
        }
    };

    absl::Status Run();
    absl::Status RunStack(TempBuffers &buffers);
    absl::StatusOr<TempBuffers> AllocateTempBuffers(int extent);
    absl::Status UpdateInput(const std::vector<int> &ids);
    void PrintParamsAndWeights() const;

    LlmWeights llm_weights_;
    LlmParams llm_params_;

    std::unique_ptr<Sampler> sampler_;

    struct RMSNormWeights {
        Halide::Runtime::Buffer<> norm_weight;
    };

    struct LayerNormWeights {
        float epsilon = 1e-5;
        Halide::Runtime::Buffer<> gamma;
        Halide::Runtime::Buffer<> beta;
    };

    using NormWeights = std::variant<RMSNormWeights, LayerNormWeights>;

    static std::optional<Llm::NormWeights> XNNNormWeightsToHalideNormWeights(
        std::optional<LlmWeights::NormWeights> &in);

public:
    // These are public only for test/benchmark purposes; don't use elsewhere.

    struct SelfAttentionWeights {
        std::optional<NormWeights> pre_norm_weight;

        Halide::Runtime::Buffer<> k_weight;
        Halide::Runtime::Buffer<> k_scale;
        Halide::Runtime::Buffer<> k_bias;
        Halide::Runtime::Buffer<> q_weight;
        Halide::Runtime::Buffer<> q_scale;
        Halide::Runtime::Buffer<> q_bias;
        Halide::Runtime::Buffer<> v_weight;
        Halide::Runtime::Buffer<> v_scale;
        Halide::Runtime::Buffer<> v_bias;
        Halide::Runtime::Buffer<> per_dim_scale;
        Halide::Runtime::Buffer<> post_proj_weight;
        Halide::Runtime::Buffer<> post_proj_scale;
        Halide::Runtime::Buffer<> post_proj_bias;

        std::optional<NormWeights> post_norm_weight;
    };

    struct FeedForwardWeights {
        std::optional<NormWeights> pre_norm_weight;
        Halide::Runtime::Buffer<> layer_1_weight;
        Halide::Runtime::Buffer<> layer_1_scale;
        Halide::Runtime::Buffer<> layer_1_bias;
        Halide::Runtime::Buffer<> layer_1_gate_weight;
        Halide::Runtime::Buffer<> layer_1_gate_scale;
        Halide::Runtime::Buffer<> layer_1_gate_bias;
        Halide::Runtime::Buffer<> layer_2_weight;
        Halide::Runtime::Buffer<> layer_2_scale;
        Halide::Runtime::Buffer<> layer_2_bias;
        std::optional<NormWeights> post_norm_weight;
    };

    const std::vector<FeedForwardWeights> &ffs() const {
        return ffs_;
    }
    const std::vector<SelfAttentionWeights> sas() const {
        return sas_;
    }
    std::optional<NormWeights> &final_norm_weight() {
        return final_norm_weight_;
    }
    Halide::Runtime::Buffer<> &softmax_linear_weights() {
        return softmax_linear_weights_;
    }
    Halide::Runtime::Buffer<> &softmax_linear_scale() {
        return softmax_linear_scale_;
    }

    Halide::Runtime::Buffer<> &segment_pos_values() {
        return segment_pos_values_;
    }
    Halide::Runtime::Buffer<> &attention_mask_values() {
        return attention_mask_values_;
    }

private:
    std::vector<FeedForwardWeights> ffs_;
    std::vector<SelfAttentionWeights> sas_;
    std::optional<NormWeights> final_norm_weight_;
    std::optional<std::shared_ptr<Tensor>> softmax_linear_f32_tensor_;
    Halide::Runtime::Buffer<> softmax_linear_f32_;
    Halide::Runtime::Buffer<> softmax_linear_weights_;
    Halide::Runtime::Buffer<> softmax_linear_scale_;
    Halide::Runtime::Buffer<> softmax_bias_;

    // Usually same as softmax_linear, but some models use different
    // softmax_linear v.s. embedding table.
    std::optional<Halide::Runtime::Buffer<>> token_embedding_;

    // Enable if enable_kv_cache
    struct KVCache {
        Halide::Runtime::Buffer<> k_cache;
        Halide::Runtime::Buffer<> v_cache;
    };

    Halide::Runtime::Buffer<> pos_embedding_;
    Halide::Runtime::Buffer<> atten_masks_;
    Halide::Runtime::Buffer<> segment_pos_;

    Halide::Runtime::Buffer<> position_embedding_values_;
    Halide::Runtime::Buffer<> attention_mask_values_;
    Halide::Runtime::Buffer<> segment_pos_values_;

    std::optional<Halide::Runtime::Buffer<>> transformer_input_;
    std::optional<Halide::Runtime::Buffer<>> logits_output_;

    // Previous ids, including prompt.
    std::vector<int> prev_ids_;
    int last_kv_cache_start_;
    std::vector<KVCache> kv_cache_;
    std::vector<int> saved_token_;
};

}  // namespace hamal
#endif  // HALIDE_APPS_HAMAL_LLM_H_
