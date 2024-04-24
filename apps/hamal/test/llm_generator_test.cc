#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include "absl/flags/flag.h"
#include "src/status_helpers.h"
// #include "third_party/absl/log/absl_check.h"
// #include "third_party/absl/log/absl_log.h"
// #include "third_party/absl/status/status.h"
// #include "third_party/halide/google/error_utils.h"
#include "src/llm.h"
// #include "third_party/odml/infra/genai/inference/halide/position_embedding.h"
// #include "third_party/odml/infra/genai/inference/halide/postprocessor.h"
// #include "third_party/odml/infra/genai/inference/halide/preprocessor.h"
// #include "third_party/odml/infra/genai/inference/halide/rope_values.h"
// #include "third_party/odml/infra/genai/inference/halide/transformer_kv_update_cache.h"
// #include "third_party/odml/infra/genai/inference/halide/transformer_kv_use_cache.h"
// #include "third_party/odml/infra/genai/inference/halide/transformer_no_kv_cache.h"
// #include "third_party/odml/infra/genai/inference/utils/llm_utils/config_utils.h"
// #include "third_party/odml/infra/genai/inference/utils/llm_utils/model_data.h"
// #include "third_party/odml/infra/genai/inference/utils/xnn_utils/benchmark_weight_accessor.h"
// #include "third_party/odml/infra/genai/inference/utils/xnn_utils/llm_weights.h"
// #include "third_party/sentencepiece/src/sentencepiece_processor.h"

ABSL_FLAG(std::optional<std::string>, model_path, std::nullopt,
          "Path to the tflite model file. Cannot be specified in conjunction "
          "with --fake_weight_type.");

// TODO just for this model?
ABSL_FLAG(int, max_tokens, 512,
          "Maximum number of input and output tokens. This value needs to be "
          "at least larger than the number of input tokens.");

ABSL_FLAG(std::string, fake_model_type, "GEMMA_2B",
          "Which ULM model to use for fake_weight_type, e.g. GEMINI_XXS, "
          "ULM1B, ULM128M. Ignored if --model_path is specified.");

ABSL_FLAG(std::string, fake_weight_type, "INT8",
          "Whether to skip loading weights from disk and use fake weights. "
          "Useful for performance benchmarking without access to the "
          "underlying model. Currently supported: INT4 and INT8. Cannot be "
          "specified in conjunction with --model_path.");

ABSL_FLAG(std::optional<std::string>, prompt, std::nullopt,
          "The input prompt to be fed to the model.");

namespace hamal {

// using llm_utils::GetCommonSessionConfig;
// using llm_utils::GetModelTypeFromFlags;
// using llm_utils::ModelData;
// using llm_utils::ScopedFile;
// using odml::infra::xnn_utils::BenchmarkMixedInt48WeightAccessor;
// using odml::infra::xnn_utils::BenchmarkWeightAccessor;
// using odml::infra::xnn_utils::LlmWeightsLoader;
// using proto::LlmModelType;
// using proto::SessionConfig;
// using sentencepiece::SentencePieceProcessor;
// using xnn_utils::LlmParams;

namespace {

absl::StatusOr<std::unique_ptr<Llm>> LoadLlm() {
  std::unique_ptr<LlmWeightsLoader> weight_loader;

  if (absl::GetFlag(FLAGS_model_path).has_value()) {
    const std::string model_path = absl::GetFlag(FLAGS_model_path).value();
    // LOG(INFO) << "Using model from path: " << model_path;

    ASSIGN_OR_RETURN(auto model_file, ScopedFile::Open(model_path));
    ASSIGN_OR_RETURN(auto model_data, ModelData::Create(std::move(model_file)));

    auto llm_params_proto = model_data->GetLlmParameters();
    auto llm_params = LlmParams::FromLLMParametersProto(llm_params_proto);
    if (!llm_params.seq_size_T) {
      llm_params.seq_size_T = absl::GetFlag(FLAGS_max_tokens);  // TODO(srj)
    }

    weight_loader = std::make_unique<xnn_utils::DefaultLlmWeightsLoader>(
        model_path, llm_params);
  } else {
    LOG(INFO) << "Constructing fake weights for testing...";

    // const LlmModelType fake_model_type =
    //     GetModelTypeFromFlags(absl::GetFlag(FLAGS_fake_model_type)).value();
    // auto session_config_or =
    //     GetCommonSessionConfig(fake_model_type,
    //     proto::SessionConfig::HALIDE);
    // if (!session_config_or.ok()) {
    //   ABSL_LOG(FATAL) << session_config_or.status();
    // }
    // SessionConfig session_config = session_config_or.value();

    LlmParams llm_params;           // TODO(srj): use defaults?
    llm_params.cache_dir = "/tmp";  // TODO(srj)
    llm_params.seq_size_T = absl::GetFlag(FLAGS_max_tokens);  // TODO(srj)

    auto fake_weights = absl::GetFlag(FLAGS_fake_weight_type);
    if (fake_weights == "INT4") {
      auto benchmark_weight_accessor =
          std::make_unique<BenchmarkMixedInt48WeightAccessor>();
      weight_loader = std::make_unique<LlmWeightsLoader>(
          std::move(benchmark_weight_accessor), llm_params);
    } else if (fake_weights == "INT8") {
      auto benchmark_weight_accessor =
          std::make_unique<BenchmarkWeightAccessor>(xnn_datatype_qcint8);
      weight_loader = std::make_unique<LlmWeightsLoader>(
          std::move(benchmark_weight_accessor), llm_params);
    } else {
      ABSL_Qassert(0) << "Unsupported fake weights mode: " << fake_weights;
    }
  }
  auto runtime_configs = std::make_unique<xnn_utils::RuntimeConfigs>();
  ASSIGN_OR_RETURN(auto llm, Llm::CreateLlm(std::move(weight_loader),
                                            std::move(runtime_configs)));
#if 1
  RETURN_IF_ERROR(llm->Reset());
  RETURN_IF_ERROR(
      llm->InitAttentionMaskValues(absl::GetFlag(FLAGS_max_tokens)));
  ABSL_Qassert(llm->attention_mask_values().data());
  ABSL_Qassert(llm->segment_pos_values().data());
#else
  std::optional<std::string> prompt = absl::GetFlag(FLAGS_prompt);
  if (!prompt.has_value()) {
    prompt.emplace("Write an email");
  }

  // TODO: this isn't right, needs to load the model
  sentencepiece::SentencePieceProcessor processor;
  std::vector<int> prompt_ids;
  RETURN_IF_ERROR(processor.Encode(*prompt, &prompt_ids));
  RETURN_IF_ERROR(llm->InitInputTokens(prompt_ids));
#endif
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
  // void TearDown() override {}

  std::unique_ptr<Llm> llm_;
};

TEST_F(LlmHalideTest, RoPEValues) {
  auto& segment_pos_values = llm_->segment_pos_values();
  CHECK_OK(StatusFromHalide(rope_values(segment_pos_values)));
}

TEST_F(LlmHalideTest, Preprocessor) {
  auto input = llm_->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto output = llm_->AllocateSeqBuffer(input.dim(1).extent());
  CHECK_OK(StatusFromHalide(preprocessor(input, output)));
}

TEST_F(LlmHalideTest, transformer_no_kv_cache) {
  auto input = llm_->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto& segment_pos_values = llm_->segment_pos_values();
  auto& attention_mask_values = llm_->attention_mask_values();
  auto output = llm_->AllocateSeqBuffer(input.dim(1).extent());
  // TODO: we only do the first entry here for now. Should we do all of them?
  ABSL_QCHECK_GT(llm_->GetLlmParams().num_transformer_M, 0);
  ABSL_QCHECK_GT(llm_->sas().size(), 0);
  ABSL_QCHECK_GT(llm_->ffs().size(), 0);
  auto sas = llm_->sas()[0];
  auto ffs = llm_->ffs()[0];

  CHECK_OK(StatusFromHalide(transformer_no_kv_cache(
      input, segment_pos_values, attention_mask_values,
      get<0>(*(ffs.pre_norm_weight)).norm_weight, sas.k_weight, sas.k_scale,
      sas.q_weight, sas.q_scale, sas.v_weight, sas.v_scale,
      sas.post_proj_weight, sas.post_proj_scale,
      get<0>(*(ffs.pre_norm_weight)).norm_weight, ffs.layer_1_weight,
      ffs.layer_1_scale, ffs.layer_1_gate_weight, ffs.layer_1_gate_scale,
      ffs.layer_2_weight, ffs.layer_2_scale, output)));
}

TEST_F(LlmHalideTest, transformer_kv_use_cache) {
  auto input = llm_->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto& segment_pos_values = llm_->segment_pos_values();
  auto& attention_mask_values = llm_->attention_mask_values();
  auto output = llm_->AllocateSeqBuffer(input.dim(1).extent());
  // TODO: we only do the first entry here for now. Should we do all of them?
  ABSL_QCHECK_GT(llm_->GetLlmParams().num_transformer_M, 0);
  ABSL_QCHECK_GT(llm_->sas().size(), 0);
  ABSL_QCHECK_GT(llm_->ffs().size(), 0);
  auto sas = llm_->sas()[0];
  auto ffs = llm_->ffs()[0];

  auto k_cache = Halide::Runtime::Buffer<float>(
      llm_->GetLlmParams().head_dim_H,
      1,  // llm_->GetLlmParams().model_dim_D /
          // llm_->GetLlmParams().head_dim_H,
      llm_->GetLlmParams().seq_size_T, llm_->GetLlmParams().batch_size_B);

  auto v_cache = Halide::Runtime::Buffer<float>(
      llm_->GetLlmParams().head_dim_H,
      1,  // llm_->GetLlmParams().model_dim_D /
          // llm_->GetLlmParams().head_dim_H,
      llm_->GetLlmParams().seq_size_T, llm_->GetLlmParams().batch_size_B);

  constexpr int last_kv_cache_start = 1;
  auto input_slice = input.cropped(1, last_kv_cache_start, 1);
  auto output_slice = output.cropped(1, last_kv_cache_start, 1);

  CHECK_OK(StatusFromHalide(transformer_kv_use_cache(
      input_slice, segment_pos_values, attention_mask_values,
      get<0>(*(sas.pre_norm_weight)).norm_weight, sas.k_weight, sas.k_scale,
      sas.q_weight, sas.q_scale, sas.v_weight, sas.v_scale,
      sas.post_proj_weight, sas.post_proj_scale,
      get<0>(*(ffs.pre_norm_weight)).norm_weight, ffs.layer_1_weight,
      ffs.layer_1_scale, ffs.layer_1_gate_weight, ffs.layer_1_gate_scale,
      ffs.layer_2_weight, ffs.layer_2_scale, k_cache, v_cache, output_slice)));
}

TEST_F(LlmHalideTest, transformer_kv_update_cache) {
  auto input = llm_->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto& segment_pos_values = llm_->segment_pos_values();
  auto& attention_mask_values = llm_->attention_mask_values();
  // TODO: we only do the first entry here for now. Should we do all of them?
  ABSL_QCHECK_GT(llm_->GetLlmParams().num_transformer_M, 0);
  ABSL_QCHECK_GT(llm_->sas().size(), 0);
  ABSL_QCHECK_GT(llm_->ffs().size(), 0);
  auto sas = llm_->sas()[0];
  auto ffs = llm_->ffs()[0];

  auto k_cache = Halide::Runtime::Buffer<float>(
      llm_->GetLlmParams().head_dim_H,
      1,  // llm_->GetLlmParams().model_dim_D /
          // llm_->GetLlmParams().head_dim_H,
      llm_->GetLlmParams().seq_size_T, llm_->GetLlmParams().batch_size_B);

  auto v_cache = Halide::Runtime::Buffer<float>(
      llm_->GetLlmParams().head_dim_H,
      1,  // llm_->GetLlmParams().model_dim_D /
          // llm_->GetLlmParams().head_dim_H,
      llm_->GetLlmParams().seq_size_T, llm_->GetLlmParams().batch_size_B);

  constexpr int last_kv_cache_start = 1;
  auto input_slice = input.cropped(1, last_kv_cache_start, 1);

  const int run_extent = input_slice.dim(1).max() - last_kv_cache_start + 1;
  auto key_slice = k_cache.cropped(2, last_kv_cache_start, run_extent);
  auto value_slice = v_cache.cropped(2, last_kv_cache_start, run_extent);

  CHECK_OK(StatusFromHalide(transformer_kv_update_cache(
      input_slice, segment_pos_values, attention_mask_values,
      get<0>(*(ffs.pre_norm_weight)).norm_weight, sas.k_weight, sas.k_scale,
      sas.q_weight, sas.q_scale, sas.v_weight, sas.v_scale,
      sas.post_proj_weight, sas.post_proj_scale, key_slice, value_slice)));
}

TEST_F(LlmHalideTest, Postprocessor) {
  auto input = llm_->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto logits_output = Halide::Runtime::Buffer<float>(
      llm_->GetLlmParams().voc_size_V, 1, llm_->GetLlmParams().batch_size_B);

  // Postprocess
  CHECK_OK(StatusFromHalide(
      postprocessor(input, get<0>(*llm_->final_norm_weight()).norm_weight,
                    llm_->softmax_linear_weights(),
                    llm_->softmax_linear_scale(), logits_output)));
}

TEST_F(LlmHalideTest, PositionEmbedding) {
  const auto& params = llm_->GetLlmParams();
  auto pos_embedding =
      Halide::Runtime::Buffer<float>(static_cast<int32_t>(params.model_dim_D),
                                     static_cast<int32_t>(params.seq_size_T));
  int32_t input_length =
      absl::GetFlag(FLAGS_max_tokens);  // TODO just for this model

  CHECK_OK(StatusFromHalide(position_embedding(input_length, params.seq_size_T,
                                               params.model_dim_D, 1.0f,
                                               10000.0f, pos_embedding)));
}

void BM_RoPEValues(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  auto& segment_pos_values = llm.value()->segment_pos_values();

  for (auto _ : state) {
    EXPECT_EQ(0, rope_values(segment_pos_values));
  }
}

void BM_Preprocessor(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  auto input = llm.value()->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto output = llm.value()->AllocateSeqBuffer(input.dim(1).extent());

  for (auto _ : state) {
    EXPECT_EQ(0, preprocessor(input, output));
  }
}

void BM_transformer_no_kv_cache(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  auto input = llm.value()->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto& segment_pos_values = llm.value()->segment_pos_values();
  auto& attention_mask_values = llm.value()->attention_mask_values();
  auto output = llm.value()->AllocateSeqBuffer(input.dim(1).extent());
  // TODO: we only do the first entry here for now. Should we do all of them?
  ABSL_QCHECK_GT(llm.value()->GetLlmParams().num_transformer_M, 0);
  ABSL_QCHECK_GT(llm.value()->sas().size(), 0);
  ABSL_QCHECK_GT(llm.value()->ffs().size(), 0);
  auto sas = llm.value()->sas()[0];
  auto ffs = llm.value()->ffs()[0];

  for (auto _ : state) {
    EXPECT_EQ(0, transformer_no_kv_cache(
                     input, segment_pos_values, attention_mask_values,
                     get<0>(*(ffs.pre_norm_weight)).norm_weight, sas.k_weight,
                     sas.k_scale, sas.q_weight, sas.q_scale, sas.v_weight,
                     sas.v_scale, sas.post_proj_weight, sas.post_proj_scale,
                     get<0>(*(ffs.pre_norm_weight)).norm_weight,
                     ffs.layer_1_weight, ffs.layer_1_scale,
                     ffs.layer_1_gate_weight, ffs.layer_1_gate_scale,
                     ffs.layer_2_weight, ffs.layer_2_scale, output));
  }
}

void BM_transformer_kv_use_cache(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  auto input = llm.value()->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto& segment_pos_values = llm.value()->segment_pos_values();
  auto& attention_mask_values = llm.value()->attention_mask_values();
  auto output = llm.value()->AllocateSeqBuffer(input.dim(1).extent());
  // TODO: we only do the first entry here for now. Should we do all of them?
  ABSL_QCHECK_GT(llm.value()->GetLlmParams().num_transformer_M, 0);
  ABSL_QCHECK_GT(llm.value()->sas().size(), 0);
  ABSL_QCHECK_GT(llm.value()->ffs().size(), 0);
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
    EXPECT_EQ(
        0, transformer_kv_use_cache(
               input_slice, segment_pos_values, attention_mask_values,
               get<0>(*(sas.pre_norm_weight)).norm_weight, sas.k_weight,
               sas.k_scale, sas.q_weight, sas.q_scale, sas.v_weight,
               sas.v_scale, sas.post_proj_weight, sas.post_proj_scale,
               get<0>(*(ffs.pre_norm_weight)).norm_weight, ffs.layer_1_weight,
               ffs.layer_1_scale, ffs.layer_1_gate_weight,
               ffs.layer_1_gate_scale, ffs.layer_2_weight, ffs.layer_2_scale,
               k_cache, v_cache, output_slice));
  }
}

void BM_transformer_kv_update_cache(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  auto input = llm.value()->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto& segment_pos_values = llm.value()->segment_pos_values();
  auto& attention_mask_values = llm.value()->attention_mask_values();
  // TODO: we only do the first entry here for now. Should we do all of them?
  ABSL_QCHECK_GT(llm.value()->GetLlmParams().num_transformer_M, 0);
  ABSL_QCHECK_GT(llm.value()->sas().size(), 0);
  ABSL_QCHECK_GT(llm.value()->ffs().size(), 0);
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
    EXPECT_EQ(0, transformer_kv_update_cache(
                     input_slice, segment_pos_values, attention_mask_values,
                     get<0>(*(ffs.pre_norm_weight)).norm_weight, sas.k_weight,
                     sas.k_scale, sas.q_weight, sas.q_scale, sas.v_weight,
                     sas.v_scale, sas.post_proj_weight, sas.post_proj_scale,
                     key_slice, value_slice));
  }
}

void BM_Postprocessor(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  auto input = llm.value()->AllocateSeqBuffer(
      absl::GetFlag(FLAGS_max_tokens));  // TODO just for this model
  auto logits_output =
      Halide::Runtime::Buffer<float>(llm.value()->GetLlmParams().voc_size_V, 1,
                                     llm.value()->GetLlmParams().batch_size_B);

  for (auto _ : state) {
    EXPECT_EQ(
        0, postprocessor(input,
                         get<0>(*llm.value()->final_norm_weight()).norm_weight,
                         llm.value()->softmax_linear_weights(),
                         llm.value()->softmax_linear_scale(), logits_output));
  }
}

void BM_PositionEmbedding(benchmark::State& state) {
  auto llm = LoadLlm();
  CHECK_OK(llm);
  const auto& params = llm.value()->GetLlmParams();
  auto pos_embedding =
      Halide::Runtime::Buffer<float>(static_cast<int32_t>(params.model_dim_D),
                                     static_cast<int32_t>(params.seq_size_T));
  int32_t input_length =
      absl::GetFlag(FLAGS_max_tokens);  // TODO just for this model

  for (auto _ : state) {
    EXPECT_EQ(0, position_embedding(input_length, params.seq_size_T,
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

}  // namespace hamal
