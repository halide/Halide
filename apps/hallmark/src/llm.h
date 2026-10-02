// TODO: license.
#ifndef HALIDE_APPS_HALLMARK_LLM_H_
#define HALIDE_APPS_HALLMARK_LLM_H_

#include <memory>

#include "HalideBuffer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "contrib/llm_params.h"
#include "contrib/llm_weights.h"
#include "contrib/sampler.h"
#include "contrib/weights_loader.h"

namespace hallmark {

class Llm {
public:
    Llm() = default;
    Llm(Llm &&) = default;
    ~Llm() = default;

    static absl::StatusOr<std::unique_ptr<Llm>> CreateLlm(
        const LlmWeights &llm_weights, const LlmParams &llm_params);

    // (Re)Initialize with input token ids. This will reset the cache, mask etc.
    absl::Status InitInputTokens(const std::vector<int> &input_ids);

    // Get the next token id.
    absl::Status GetNextToken(std::vector<int> *output_ids);

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
            return first ? initial_input : buffers[current_input];
        }

        Halide::Runtime::Buffer<> &CurrentOutput() {
            return first ? buffers[0] : buffers[current_input ^ 1];
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
    absl::Status UpdateInput(const std::vector<int> &ids);
    void PrintParamsAndWeights() const;

    LlmWeights llm_weights_;
    LlmParams llm_params_;

    std::unique_ptr<Sampler> sampler_;

public:
    const std::vector<LlmWeights::FeedForwardWeights> &ffs() const {
        return llm_weights_.ffs;
    }
    const std::vector<LlmWeights::SelfAttentionWeights> sas() const {
        return llm_weights_.sas;
    }
    std::optional<LlmWeights::NormWeights> &final_norm_weight() {
        return llm_weights_.final_norm_weight;
    }
    Halide::Runtime::Buffer<> &softmax_linear_weights() {
        return llm_weights_.softmax_linear.weights;
    }
    Halide::Runtime::Buffer<> &softmax_linear_scale() {
        return llm_weights_.softmax_linear.scale;
    }

    Halide::Runtime::Buffer<> &segment_pos_values() {
        return segment_pos_values_;
    }
    Halide::Runtime::Buffer<> &attention_mask_values() {
        return attention_mask_values_;
    }

private:
    Halide::Runtime::Buffer<> softmax_linear_f32_;

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

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_LLM_H_
