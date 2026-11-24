#include "contrib/weights_loader.h"

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "contrib/memory_mapped_file.h"
#include "contrib/status_helpers.h"
// macOS system headers #define this value in syslimits.h
#undef ARG_MAX
#include "contrib/tflite_schema_generated.h"

namespace hallmark {

namespace {

class DataHolderMemoryMappedFile : public DataHolder {
public:
    explicit DataHolderMemoryMappedFile(absl::string_view path)
        : file(path) {
    }
    MemoryMappedFile file;
};

using FeedForwardWeights = LlmWeights::FeedForwardWeights;
using SelfAttentionWeights = LlmWeights::SelfAttentionWeights;

class LlmWeightsLoader {
public:
    LlmWeightsLoader(absl::string_view weight_path, const LlmParams &params);

    absl::StatusOr<LlmWeights> LoadWeights();

private:
    LlmParams params_;
    std::shared_ptr<DataHolderMemoryMappedFile> mapped_file_;
    absl::flat_hash_map<absl::string_view, Halide::Runtime::Buffer<>> weights_;

    absl::StatusOr<LlmWeights::SelfAttentionWeights> LoadSelfAttention(
        int layer_id);

    absl::StatusOr<LlmWeights::FeedForwardWeights> LoadFeedForward(int layer_id);

    absl::StatusOr<ScaledTensor> LoadWeight(absl::string_view tensor_name,
                                            std::vector<int> expected_dims,
                                            size_t dim_scale_if_any = 0) const;

    absl::StatusOr<ScaledTensor> LoadTransposedWeight(
        absl::string_view tensor_name, std::vector<int> expected_dims,
        size_t dim_scale_if_any) const;

    absl::StatusOr<std::optional<LlmWeights::NormWeights>> LoadNormWeights(
        LlmParams::Norm norm_type, absl::string_view basename);

    // is_query: indicating whether the weight is for query projection or not.
    // Note that the key/value projection weights are handled differently between
    // MHA vs. MQA.
    absl::StatusOr<ScaledTensor> TryCacheThenLoadSelfAttention(
        absl::string_view filename_prefix, absl::string_view alt_filename_prefix,
        bool is_query);

    void BuildWeightsMapFromTfliteModel(char *data);
};

// According to norm_type, load necessary weights with given basename.
absl::StatusOr<std::optional<LlmWeights::NormWeights>>
LlmWeightsLoader::LoadNormWeights(LlmParams::Norm norm_type,
                                  absl::string_view basename) {
    switch (norm_type) {
    case LlmParams::Norm::UNSPECIFIED:
        break;
    case LlmParams::Norm::NO_NORM:
        break;
    case LlmParams::Norm::RMS_NORM: {
        auto rms_norm_weights = RMSNormWeights();
        ASSIGN_OR_RETURN(rms_norm_weights.norm_weight,
                         LoadWeight(absl::StrCat(basename, ".scale"),
                                    {(int)params_.model_dim_D}));
        return rms_norm_weights;
    }
    case LlmParams::Norm::LAYER_NORM: {
        auto layer_norm_weights = LayerNormWeights();
        ASSIGN_OR_RETURN(layer_norm_weights.beta,
                         LoadWeight(absl::StrCat(basename, ".bias"),
                                    {1, 1, (int)params_.model_dim_D}));
        ASSIGN_OR_RETURN(layer_norm_weights.gamma,
                         LoadWeight(absl::StrCat(basename, ".scale"),
                                    {1, 1, (int)params_.model_dim_D}));
        return layer_norm_weights;
    }
    default:
        break;
    }
    return std::nullopt;
}

void LlmWeightsLoader::BuildWeightsMapFromTfliteModel(char *data) {
    auto *tflite_model = ::tflite::GetModel(mapped_file_->file.data());
    const auto *buffers = tflite_model->buffers();
    for (const auto *subgraph : *tflite_model->subgraphs()) {
        for (const auto *tfl_tensor : *subgraph->tensors()) {
            auto tensor_name = absl::string_view(tfl_tensor->name()->data(),
                                                 tfl_tensor->name()->size());
            halide_type_t halide_type;
            CHECK(tfl_tensor->buffer() < buffers->size());
            const ::tflite::Buffer &tfl_buffer = *buffers->Get(tfl_tensor->buffer());
            switch (tfl_tensor->type()) {
            case ::tflite::TensorType::FLOAT32:
                halide_type = halide_type_t(halide_type_float, 32);
                break;
            case ::tflite::TensorType::INT8:
                halide_type = halide_type_t(halide_type_int, 8);
                break;
            case ::tflite::TensorType::INT4:
                halide_type = halide_type_t(halide_type_int, 4);
                break;
            default:
                std::cerr << "Unsupported tensor type: " << (int) tfl_tensor->type();
                std::abort();
                break;
            }

            std::vector<int> tfl_dims(tfl_tensor->shape()->begin(),
                                      tfl_tensor->shape()->end());
            // Halide convention has dims in opposite order of Tensor.
            std::vector<int> halide_dims;
            halide_dims.reserve(tfl_dims.size());
            for (size_t i = tfl_dims.size(); i > 0; i--) {
                halide_dims.push_back(static_cast<int>(tfl_dims[i - 1]));
            }

            weights_[tensor_name] = Halide::Runtime::Buffer<>(
                halide_type, data + tfl_buffer.offset(), halide_dims);
        }
    }
}

absl::StatusOr<ScaledTensor> LlmWeightsLoader::LoadWeight(
    absl::string_view tensor_name, std::vector<int> expected_dims,
    size_t dim_scale_if_any) const {
    if (!weights_.contains(tensor_name)) {
        // LOG(WARNING) << "Tensor not found: " << tensor_name;
        return ScaledTensor();
    }
    ScaledTensor result;
    result.weights = weights_.at(tensor_name);
    // Check dimensions.
    {
        bool correct_dimension = true;
        const int d = result.weights.dimensions();
        correct_dimension &= (d == expected_dims.size());
        // Note that 'expected' is in the reverse order of what we expect.
        for (int i = 0; i < d; ++i) {
            correct_dimension &=
                (result.weights.dim(i).extent() == expected_dims[d - i - 1]);
        }
        if (!correct_dimension) {
            return absl::InvalidArgumentError(
                absl::StrCat("Dimension mismatch for ", tensor_name));
        }
    }

    if (result.weights.type().code == halide_type_float) {
        result.scale = Halide::Runtime::Buffer<>();
        return result;
    }

    // Following are logic for quantized weights.
    std::string scale_tensor_name = absl::StrCat(tensor_name, "_quantized_scale");
    if (!weights_.contains(scale_tensor_name)) {
        return absl::NotFoundError(
            absl::StrCat("Scale tensor not found: ", scale_tensor_name));
    }
    result.scale = weights_.at(scale_tensor_name);
    result.dim_scale = dim_scale_if_any;
    if (expected_dims[dim_scale_if_any] != result.scale.number_of_elements()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Dimension mismatch for ", scale_tensor_name));
    }
    return result;
}

absl::StatusOr<ScaledTensor> LlmWeightsLoader::LoadTransposedWeight(
    absl::string_view tensor_name, std::vector<int> expected_dims,
    size_t dim_scale_if_any) const {
    return LoadWeight(
        tensor_name,
        std::vector<int>(expected_dims.rbegin(), expected_dims.rend()),
        1 - dim_scale_if_any);
}

LlmWeightsLoader::LlmWeightsLoader(absl::string_view weight_path,
                                   const LlmParams &params)
    : params_(params) {
    mapped_file_ = std::make_shared<DataHolderMemoryMappedFile>(weight_path);
    if (mapped_file_->file.valid()) {
        BuildWeightsMapFromTfliteModel(static_cast<char *>(mapped_file_->file.data()));
    }
}

absl::StatusOr<ScaledTensor> LlmWeightsLoader::TryCacheThenLoadSelfAttention(
    absl::string_view filename_prefix, absl::string_view alt_filename_prefix,
    bool is_query) {
    ScaledTensor r;
    if (!is_query) {
        ASSIGN_OR_RETURN(r, LoadTransposedWeight(filename_prefix,
                                                 {(int)params_.model_dim_D,
                                                  (int)params_.num_kv_heads *
                                                      (int)params_.head_dim_H},
                                                 1));
        if (!r.weights.data()) {
            ASSIGN_OR_RETURN(r, LoadTransposedWeight(alt_filename_prefix,
                                                     {(int)params_.model_dim_D,
                                                      (int)params_.num_kv_heads *
                                                          (int)params_.head_dim_H},
                                                     1));
        }
        // r->SetMetadata("self_attention_reshaped_weight_N", params_.num_kv_heads);
    } else {
        ASSIGN_OR_RETURN(r, LoadTransposedWeight(
                                filename_prefix,
                                {(int)params_.model_dim_D,
                                 (int)params_.n_heads_N * (int)params_.head_dim_H},
                                1));
        if (!r.weights.data()) {
            ASSIGN_OR_RETURN(r, LoadTransposedWeight(alt_filename_prefix,
                                                     {(int)params_.model_dim_D,
                                                      (int)params_.n_heads_N *
                                                          (int)params_.head_dim_H},
                                                     1));
        }
        // r->SetMetadata("self_attention_reshaped_weight_N", params_.n_heads_N);
    }
    // r->SetMetadata("in_dim_last_in_weight", 1);
    return r;
}

absl::StatusOr<FeedForwardWeights> LlmWeightsLoader::LoadFeedForward(
    int layer_id) {
    const auto &params = params_;
    auto ff_file_prefix =
        absl::StrCat("params.lm.transformer.x_layers_", layer_id, ".ff_layer.");
    FeedForwardWeights feed_forward;

    ASSIGN_OR_RETURN(
        feed_forward.pre_norm_weight,
        LoadNormWeights(params.ff_params.pre_norm,
                        absl::StrCat(ff_file_prefix, "pre_layer_norm")));

    ASSIGN_OR_RETURN(
        feed_forward.post_norm_weight,
        LoadNormWeights(params.ff_params.post_norm,
                        absl::StrCat(ff_file_prefix, "post_layer_norm")));

    ASSIGN_OR_RETURN(
        feed_forward.layer_1_weight,
        LoadTransposedWeight(absl::StrCat(ff_file_prefix, "ffn_layer1.w"),
                             {(int)params.model_dim_D, (int)params.hidden_dim_HD},
                             /*original_dim_scale=*/1));
    if (!feed_forward.layer_1_weight.weights.data()) {
        ASSIGN_OR_RETURN(feed_forward.layer_1_weight,
                         LoadTransposedWeight(
                             absl::StrCat(ff_file_prefix, "ffn_layer1.linear.w"),
                             {(int)params.model_dim_D, (int)params.hidden_dim_HD},
                             /*original_dim_scale=*/1));
    }
    ASSIGN_OR_RETURN(
        feed_forward.layer_1_gate_weight,
        LoadTransposedWeight(absl::StrCat(ff_file_prefix, "ffn_layer1_gate.w"),
                             {(int)params.model_dim_D, (int)params.hidden_dim_HD},
                             /*original_dim_scale=*/1));
    if (!feed_forward.layer_1_gate_weight.weights.data()) {
        ASSIGN_OR_RETURN(
            feed_forward.layer_1_gate_weight,
            LoadTransposedWeight(
                absl::StrCat(ff_file_prefix, "ffn_layer1_gate.linear.w"),
                {(int)params.model_dim_D, (int)params.hidden_dim_HD},
                /*original_dim_scale=*/1));
    }
    ASSIGN_OR_RETURN(
        feed_forward.layer_2_weight,
        LoadTransposedWeight(absl::StrCat(ff_file_prefix, "ffn_layer2.w"),
                             {(int)params.hidden_dim_HD, (int)params.model_dim_D},
                             /*original_dim_scale=*/1));
    if (!feed_forward.layer_2_weight.weights.data()) {
        ASSIGN_OR_RETURN(feed_forward.layer_2_weight,
                         LoadTransposedWeight(
                             absl::StrCat(ff_file_prefix, "ffn_layer2.linear.w"),
                             {(int)params.hidden_dim_HD, (int)params.model_dim_D},
                             /*original_dim_scale=*/1));
    }

    if (!params.ff_params.no_bias) {
        ASSIGN_OR_RETURN(
            feed_forward.layer_1_bias,
            LoadWeight(absl::StrCat(ff_file_prefix, "ffn_layer1.bias.b"),
                       {(int)params.hidden_dim_HD}));
        ASSIGN_OR_RETURN(
            feed_forward.layer_1_gate_bias,
            LoadWeight(absl::StrCat(ff_file_prefix, "ffn_layer1_gate.bias.b"),
                       {(int)params.hidden_dim_HD}));
        ASSIGN_OR_RETURN(
            feed_forward.layer_2_bias,
            LoadWeight(absl::StrCat(ff_file_prefix, "ffn_layer2.bias.b"),
                       {(int)params.model_dim_D}));
    }

    return feed_forward;
}

absl::StatusOr<SelfAttentionWeights> LlmWeightsLoader::LoadSelfAttention(
    int layer_id) {
    const auto &params = params_;
    SelfAttentionWeights self_attention;

    auto sa_file_prefix =
        absl::StrCat("params.lm.transformer.x_layers_", layer_id);

    ASSIGN_OR_RETURN(
        self_attention.pre_norm_weight,
        LoadNormWeights(params.sa_params.pre_norm,
                        absl::StrCat(sa_file_prefix, ".pre_layer_norm")));
    ASSIGN_OR_RETURN(
        self_attention.post_norm_weight,
        LoadNormWeights(params.sa_params.post_norm,
                        absl::StrCat(sa_file_prefix, ".post_layer_norm")));

    absl::StrAppend(&sa_file_prefix, ".self_attention.");

    ASSIGN_OR_RETURN(
        self_attention.k_weight,
        TryCacheThenLoadSelfAttention(absl::StrCat(sa_file_prefix, "k.w"),
                                      absl::StrCat(sa_file_prefix, "k.linear.w"),
                                      /*is_query=*/false));
    ASSIGN_OR_RETURN(
        self_attention.q_weight,
        TryCacheThenLoadSelfAttention(absl::StrCat(sa_file_prefix, "q.w"),
                                      absl::StrCat(sa_file_prefix, "q.linear.w"),
                                      /*is_query=*/true));
    ASSIGN_OR_RETURN(
        self_attention.v_weight,
        TryCacheThenLoadSelfAttention(absl::StrCat(sa_file_prefix, "v.w"),
                                      absl::StrCat(sa_file_prefix, "v.linear.w"),
                                      /*is_query=*/false));

    if (!params.sa_params.qkv_no_bias) {
        ASSIGN_OR_RETURN(
            self_attention.q_bias,
            LoadWeight(absl::StrCat(sa_file_prefix, "q.bias.b"),
                       {(int)params.n_heads_N * (int)params.head_dim_H}));
        ASSIGN_OR_RETURN(
            self_attention.k_bias,
            LoadWeight(absl::StrCat(sa_file_prefix, "k.bias.b"),
                       {(int)params.n_heads_N * (int)params.head_dim_H}));
        ASSIGN_OR_RETURN(
            self_attention.v_bias,
            LoadWeight(absl::StrCat(sa_file_prefix, "v.bias.b"),
                       {(int)params.n_heads_N * (int)params.head_dim_H}));
    }

    if (params.sa_params.attention_scale_type ==
        LlmParams::AttentionScaleType::PER_DIM_SCALE) {
        ASSIGN_OR_RETURN(
            self_attention.per_dim_scale,
            LoadWeight(absl::StrCat(sa_file_prefix, "per_dim_scale.per_dim_scale"),
                       {(int)params.head_dim_H}));
    }
    ASSIGN_OR_RETURN(self_attention.post_proj_weight,
                     LoadWeight(absl::StrCat(sa_file_prefix, "post.w"),
                                {(int)params.model_dim_D,
                                 (int)params.n_heads_N * (int)params.head_dim_H},
                                /*dim_scale_if_any=*/0));
    if (!self_attention.post_proj_weight.weights.data()) {
        ASSIGN_OR_RETURN(
            self_attention.post_proj_weight,
            LoadWeight(absl::StrCat(sa_file_prefix, "post.linear.w"),
                       {(int)params.model_dim_D,
                        (int)params.n_heads_N * (int)params.head_dim_H},
                       /*dim_scale_if_any=*/0));
    }
    if (!params.sa_params.post_proj_no_bias) {
        ASSIGN_OR_RETURN(self_attention.post_proj_bias,
                         LoadWeight(absl::StrCat(sa_file_prefix, "post.bias.b"),
                                    {(int)params.model_dim_D}));
    }

    return self_attention;
}

absl::StatusOr<LlmWeights> LlmWeightsLoader::LoadWeights() {
    LlmWeights result;

    for (int layer_id = 0; layer_id < params_.num_transformer_M; ++layer_id) {
        FeedForwardWeights ff;
        ASSIGN_OR_RETURN(ff, LoadFeedForward(layer_id));
        result.ffs.push_back(std::move(ff));
        SelfAttentionWeights sa;
        ASSIGN_OR_RETURN(sa, LoadSelfAttention(layer_id));
        result.sas.push_back(std::move(sa));
    }

    ASSIGN_OR_RETURN(result.final_norm_weight,
                     LoadNormWeights(params_.final_norm, "params.lm.final_ln"));

    ASSIGN_OR_RETURN(result.softmax_linear,
                     LoadTransposedWeight(
                         "params.lm.softmax.logits_ffn.w",
                         {(int)params_.model_dim_D, (int)params_.voc_size_V}, 1));
    if (!result.softmax_linear.weights.data()) {
        ASSIGN_OR_RETURN(
            result.softmax_linear,
            LoadTransposedWeight(
                "params.lm.softmax.logits_ffn.linear.w",
                {(int)params_.model_dim_D, (int)params_.voc_size_V}, 1));
    }
    if (!params_.final_proj_params.no_bias) {
        ASSIGN_OR_RETURN(result.softmax_bias,
                         LoadWeight("params.lm.softmax.logits_ffn.bias.b",
                                    {(int)params_.voc_size_V}));
    }

    ASSIGN_OR_RETURN(
        result.token_embedding,
        LoadWeight("params.lm.token_embedding.w",
                   {(int)params_.voc_size_V, (int)params_.model_dim_D}));

    result.data_holder = mapped_file_;
    return result;
}

}  // namespace

absl::StatusOr<LlmWeights> LoadLlmWeights(absl::string_view tflite_path, const LlmParams &params) {
    LlmWeightsLoader loader(tflite_path, params);
    return loader.LoadWeights();
}

}  // namespace hallmark
