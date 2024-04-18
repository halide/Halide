#include <gtest/gtest.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "contrib/llm_weights.h"
#include "contrib/status_helpers.h"
#include "hallmark_position_embedding.h"
#include "hallmark_postprocessor.h"
#include "hallmark_preprocessor.h"
#include "hallmark_rope_values.h"
#include "hallmark_transformer_kv_update_cache.h"
#include "hallmark_transformer_kv_use_cache.h"
#include "hallmark_transformer_no_kv_cache.h"
#include "src/llm.h"

ABSL_FLAG(std::optional<std::string>, model_path, std::nullopt,
          "Path to the tflite model file.");

// TODO just for this model?
ABSL_FLAG(int, max_tokens, 512,
          "Maximum number of input and output tokens. This value needs to be "
          "at least larger than the number of input tokens.");

namespace hallmark {

namespace {

absl::StatusOr<std::unique_ptr<Llm>> LoadLlm() {
    CHECK(absl::GetFlag(FLAGS_model_path).has_value());

    const std::string model_path = absl::GetFlag(FLAGS_model_path).value();
    // LOG(INFO) << "Using model from path: " << model_path;

    auto p = LoadLlmParams(model_path);
    if (!p.ok()) {
        return p.status();
    }
    auto llm_params = std::move(p.value());
    llm_params.seq_size_T = absl::GetFlag(FLAGS_max_tokens);  // TODO: not sure about this

    auto w = LoadLlmWeights(model_path, llm_params);
    if (!w.ok()) {
        return w.status();
    }
    auto llm_weights = std::move(w.value());

    auto l = Llm::CreateLlm(llm_weights, llm_params);
    if (!l.ok()) {
        return l.status();
    }
    auto llm = std::move(l.value());

    RETURN_IF_ERROR(llm->Reset());
    RETURN_IF_ERROR(
        llm->InitAttentionMaskValues(llm_params.seq_size_T));

    return llm;
}
}  // namespace

class LlmHalideTest : public testing::Test {
protected:
    void SetUp() override {
        auto llm = LoadLlm();
        CHECK_OK(llm);
        llm_ = std::move(*llm);
    }

    void TearDown() override {
        // nothing
    }

    std::unique_ptr<Llm> llm_;
};

TEST_F(LlmHalideTest, RoPEValues) {
    auto &segment_pos_values = llm_->segment_pos_values();
    CHECK_OK(StatusFromHalide(rope_values(segment_pos_values)));
}

TEST_F(LlmHalideTest, Preprocessor) {
    auto input = llm_->AllocateSeqBuffer(
        llm_->GetLlmParams().seq_size_T);  // TODO just for this model
    auto output = llm_->AllocateSeqBuffer(input.dim(1).extent());
    CHECK_OK(StatusFromHalide(preprocessor(input, output)));
}

TEST_F(LlmHalideTest, transformer_no_kv_cache) {
    auto input = llm_->AllocateSeqBuffer(
        llm_->GetLlmParams().seq_size_T);  // TODO just for this model
    auto &segment_pos_values = llm_->segment_pos_values();
    auto &attention_mask_values = llm_->attention_mask_values();
    auto output = llm_->AllocateSeqBuffer(input.dim(1).extent());
    // TODO: we only do the first entry here for now. Should we do all of them?
    auto sas = llm_->sas()[0];
    auto ffs = llm_->ffs()[0];

    CHECK_OK(StatusFromHalide(transformer_no_kv_cache(
        input, segment_pos_values, attention_mask_values,
        std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights, sas.k_weight.weights,
        sas.k_weight.scale, sas.q_weight.weights, sas.q_weight.scale,
        sas.v_weight.weights, sas.v_weight.scale, sas.post_proj_weight.weights,
        sas.post_proj_weight.scale,
        std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights,
        ffs.layer_1_weight.weights, ffs.layer_1_weight.scale,
        ffs.layer_1_gate_weight.weights, ffs.layer_1_gate_weight.scale,
        ffs.layer_2_weight.weights, ffs.layer_2_weight.scale, output)));
}

TEST_F(LlmHalideTest, transformer_kv_use_cache) {
    auto input = llm_->AllocateSeqBuffer(
        llm_->GetLlmParams().seq_size_T);  // TODO just for this model
    auto &segment_pos_values = llm_->segment_pos_values();
    auto &attention_mask_values = llm_->attention_mask_values();
    auto output = llm_->AllocateSeqBuffer(input.dim(1).extent());
    // TODO: we only do the first entry here for now. Should we do all of them?
    auto sas = llm_->sas()[0];
    auto ffs = llm_->ffs()[0];

    auto k_cache = Halide::Runtime::Buffer<float>(
        llm_->GetLlmParams().head_dim_H, 1, llm_->GetLlmParams().seq_size_T,
        llm_->GetLlmParams().batch_size_B);

    auto v_cache = Halide::Runtime::Buffer<float>(
        llm_->GetLlmParams().head_dim_H, 1, llm_->GetLlmParams().seq_size_T,
        llm_->GetLlmParams().batch_size_B);

    constexpr int last_kv_cache_start = 1;
    auto input_slice = input.cropped(1, last_kv_cache_start, 1);
    auto output_slice = output.cropped(1, last_kv_cache_start, 1);

    CHECK_OK(StatusFromHalide(transformer_kv_use_cache(
        input_slice, segment_pos_values, attention_mask_values,
        std::get<0>(*(sas.pre_norm_weight)).norm_weight.weights, sas.k_weight.weights,
        sas.k_weight.scale, sas.q_weight.weights, sas.q_weight.scale,
        sas.v_weight.weights, sas.v_weight.scale, sas.post_proj_weight.weights,
        sas.post_proj_weight.scale,
        std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights,
        ffs.layer_1_weight.weights, ffs.layer_1_weight.scale,
        ffs.layer_1_gate_weight.weights, ffs.layer_1_gate_weight.scale,
        ffs.layer_2_weight.weights, ffs.layer_2_weight.scale, k_cache, v_cache,
        output_slice)));
}

TEST_F(LlmHalideTest, transformer_kv_update_cache) {
    auto input = llm_->AllocateSeqBuffer(
        llm_->GetLlmParams().seq_size_T);  // TODO just for this model
    auto &segment_pos_values = llm_->segment_pos_values();
    auto &attention_mask_values = llm_->attention_mask_values();
    // TODO: we only do the first entry here for now. Should we do all of them?
    auto sas = llm_->sas()[0];
    auto ffs = llm_->ffs()[0];

    auto k_cache = Halide::Runtime::Buffer<float>(
        llm_->GetLlmParams().head_dim_H, 1, llm_->GetLlmParams().seq_size_T,
        llm_->GetLlmParams().batch_size_B);

    auto v_cache = Halide::Runtime::Buffer<float>(
        llm_->GetLlmParams().head_dim_H, 1, llm_->GetLlmParams().seq_size_T,
        llm_->GetLlmParams().batch_size_B);

    constexpr int last_kv_cache_start = 1;
    auto input_slice = input.cropped(1, last_kv_cache_start, 1);

    const int run_extent = input_slice.dim(1).max() - last_kv_cache_start + 1;
    auto key_slice = k_cache.cropped(2, last_kv_cache_start, run_extent);
    auto value_slice = v_cache.cropped(2, last_kv_cache_start, run_extent);

    CHECK_OK(StatusFromHalide(transformer_kv_update_cache(
        input_slice, segment_pos_values, attention_mask_values,
        std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights, sas.k_weight.weights,
        sas.k_weight.scale, sas.q_weight.weights, sas.q_weight.scale,
        sas.v_weight.weights, sas.v_weight.scale, sas.post_proj_weight.weights,
        sas.post_proj_weight.scale, key_slice, value_slice)));
}

TEST_F(LlmHalideTest, Postprocessor) {
    auto input = llm_->AllocateSeqBuffer(
        llm_->GetLlmParams().seq_size_T);  // TODO just for this model
    auto logits_output = Halide::Runtime::Buffer<float>(
        llm_->GetLlmParams().voc_size_V, 1, llm_->GetLlmParams().batch_size_B);

    // Postprocess
    CHECK_OK(StatusFromHalide(postprocessor(
        input, std::get<0>(*llm_->final_norm_weight()).norm_weight.weights,
        llm_->softmax_linear_weights(), llm_->softmax_linear_scale(),
        logits_output)));
}

TEST_F(LlmHalideTest, PositionEmbedding) {
    const auto &params = llm_->GetLlmParams();
    auto pos_embedding =
        Halide::Runtime::Buffer<float>(static_cast<int32_t>(params.model_dim_D),
                                       static_cast<int32_t>(params.seq_size_T));
    int32_t input_length = params.seq_size_T;
    CHECK_OK(StatusFromHalide(position_embedding(input_length, params.seq_size_T,
                                                 params.model_dim_D, 1.0f,
                                                 10000.0f, pos_embedding)));
}

}  // namespace hallmark

// gtest's main() won't initialize Abseil flags, so we must define our own
int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
