#include <benchmark/benchmark.h>

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

void BM_RoPEValues(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    auto &segment_pos_values = llm.value()->segment_pos_values();

    for (auto _ : state) {
        CHECK_EQ(0, rope_values(segment_pos_values));
    }
}

void BM_Preprocessor(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    auto input = llm.value()->AllocateSeqBuffer(
        llm.value()->GetLlmParams().seq_size_T);  // TODO just for this model
    auto output = llm.value()->AllocateSeqBuffer(input.dim(1).extent());

    for (auto _ : state) {
        CHECK_EQ(0, preprocessor(input, output));
    }
}

void BM_transformer_no_kv_cache(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    auto input = llm.value()->AllocateSeqBuffer(
        llm.value()->GetLlmParams().seq_size_T);  // TODO just for this model
    auto &segment_pos_values = llm.value()->segment_pos_values();
    auto &attention_mask_values = llm.value()->attention_mask_values();
    auto output = llm.value()->AllocateSeqBuffer(input.dim(1).extent());
    // TODO: we only do the first entry here for now. Should we do all of them?
    auto sas = llm.value()->sas()[0];
    auto ffs = llm.value()->ffs()[0];

    for (auto _ : state) {
        CHECK_EQ(
            0, transformer_no_kv_cache(
                   input, segment_pos_values, attention_mask_values,
                   std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights,
                   sas.k_weight.weights, sas.k_weight.scale, sas.q_weight.weights,
                   sas.q_weight.scale, sas.v_weight.weights, sas.v_weight.scale,
                   sas.post_proj_weight.weights, sas.post_proj_weight.scale,
                   std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights,
                   ffs.layer_1_weight.weights, ffs.layer_1_weight.scale,
                   ffs.layer_1_gate_weight.weights, ffs.layer_1_gate_weight.scale,
                   ffs.layer_2_weight.weights, ffs.layer_2_weight.scale, output));
    }
}

void BM_transformer_kv_use_cache(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    auto input = llm.value()->AllocateSeqBuffer(
        llm.value()->GetLlmParams().seq_size_T);  // TODO just for this model
    auto &segment_pos_values = llm.value()->segment_pos_values();
    auto &attention_mask_values = llm.value()->attention_mask_values();
    auto output = llm.value()->AllocateSeqBuffer(input.dim(1).extent());
    // TODO: we only do the first entry here for now. Should we do all of them?
    auto sas = llm.value()->sas()[0];
    auto ffs = llm.value()->ffs()[0];

    auto k_cache = Halide::Runtime::Buffer<float>(
        llm.value()->GetLlmParams().head_dim_H,
        1,  // llm.value()->GetLlmParams().model_dim_D /
            // llm.value()->GetLlmParams().head_dim_H,
        llm.value()->GetLlmParams().seq_size_T,
        llm.value()->GetLlmParams().batch_size_B);

    auto v_cache = Halide::Runtime::Buffer<float>(
        llm.value()->GetLlmParams().head_dim_H,
        1,  // llm.value()->GetLlmParams().model_dim_D /
            // llm.value()->GetLlmParams().head_dim_H,
        llm.value()->GetLlmParams().seq_size_T,
        llm.value()->GetLlmParams().batch_size_B);

    constexpr int last_kv_cache_start = 1;
    auto input_slice = input.cropped(1, last_kv_cache_start, 1);
    auto output_slice = output.cropped(1, last_kv_cache_start, 1);

    for (auto _ : state) {
        CHECK_EQ(
            0, transformer_kv_use_cache(
                   input_slice, segment_pos_values, attention_mask_values,
                   std::get<0>(*(sas.pre_norm_weight)).norm_weight.weights,
                   sas.k_weight.weights, sas.k_weight.scale, sas.q_weight.weights,
                   sas.q_weight.scale, sas.v_weight.weights, sas.v_weight.scale,
                   sas.post_proj_weight.weights, sas.post_proj_weight.scale,
                   std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights,
                   ffs.layer_1_weight.weights, ffs.layer_1_weight.scale,
                   ffs.layer_1_gate_weight.weights, ffs.layer_1_gate_weight.scale,
                   ffs.layer_2_weight.weights, ffs.layer_2_weight.scale, k_cache,
                   v_cache, output_slice));
    }
}

void BM_transformer_kv_update_cache(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    auto input = llm.value()->AllocateSeqBuffer(
        llm.value()->GetLlmParams().seq_size_T);  // TODO just for this model
    auto &segment_pos_values = llm.value()->segment_pos_values();
    auto &attention_mask_values = llm.value()->attention_mask_values();
    // TODO: we only do the first entry here for now. Should we do all of them?
    auto sas = llm.value()->sas()[0];
    auto ffs = llm.value()->ffs()[0];

    auto k_cache = Halide::Runtime::Buffer<float>(
        llm.value()->GetLlmParams().head_dim_H,
        1,  // llm.value()->GetLlmParams().model_dim_D /
            // llm.value()->GetLlmParams().head_dim_H,
        llm.value()->GetLlmParams().seq_size_T,
        llm.value()->GetLlmParams().batch_size_B);

    auto v_cache = Halide::Runtime::Buffer<float>(
        llm.value()->GetLlmParams().head_dim_H,
        1,  // llm.value()->GetLlmParams().model_dim_D /
            // llm.value()->GetLlmParams().head_dim_H,
        llm.value()->GetLlmParams().seq_size_T,
        llm.value()->GetLlmParams().batch_size_B);

    constexpr int last_kv_cache_start = 1;
    auto input_slice = input.cropped(1, last_kv_cache_start, 1);

    const int run_extent = input_slice.dim(1).max() - last_kv_cache_start + 1;
    auto key_slice = k_cache.cropped(2, last_kv_cache_start, run_extent);
    auto value_slice = v_cache.cropped(2, last_kv_cache_start, run_extent);

    for (auto _ : state) {
        CHECK_EQ(
            0, transformer_kv_update_cache(
                   input_slice, segment_pos_values, attention_mask_values,
                   std::get<0>(*(ffs.pre_norm_weight)).norm_weight.weights,
                   sas.k_weight.weights, sas.k_weight.scale, sas.q_weight.weights,
                   sas.q_weight.scale, sas.v_weight.weights, sas.v_weight.scale,
                   sas.post_proj_weight.weights, sas.post_proj_weight.scale,
                   key_slice, value_slice));
    }
}

void BM_Postprocessor(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    auto input = llm.value()->AllocateSeqBuffer(
        llm.value()->GetLlmParams().seq_size_T);  // TODO just for this model
    auto logits_output =
        Halide::Runtime::Buffer<float>(llm.value()->GetLlmParams().voc_size_V, 1,
                                       llm.value()->GetLlmParams().batch_size_B);

    for (auto _ : state) {
        CHECK_EQ(0,
                 postprocessor(
                     input,
                     std::get<0>(*llm.value()->final_norm_weight()).norm_weight.weights,
                     llm.value()->softmax_linear_weights(),
                     llm.value()->softmax_linear_scale(), logits_output));
    }
}

void BM_PositionEmbedding(benchmark::State &state) {
    auto llm = LoadLlm();
    CHECK_OK(llm);
    const auto &params = llm.value()->GetLlmParams();
    auto pos_embedding =
        Halide::Runtime::Buffer<float>(static_cast<int32_t>(params.model_dim_D),
                                       static_cast<int32_t>(params.seq_size_T));
    int32_t input_length =
        llm.value()->GetLlmParams().seq_size_T;  // TODO just for this model

    for (auto _ : state) {
        CHECK_EQ(0, position_embedding(input_length, params.seq_size_T,
                                       params.model_dim_D, 1.0f, 10000.0f,
                                       pos_embedding));
    }
}

BENCHMARK(BM_Preprocessor);
BENCHMARK(BM_transformer_no_kv_cache);
BENCHMARK(BM_transformer_kv_use_cache);
BENCHMARK(BM_transformer_kv_update_cache);
BENCHMARK(BM_Postprocessor);
BENCHMARK(BM_PositionEmbedding);

}  // namespace hallmark

// gtest's main() won't initialize Abseil flags, so we must define our own
int main(int argc, char **argv) {
    benchmark::Initialize(&argc, argv);
    absl::ParseCommandLine(argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
