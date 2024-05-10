#include "Halide.h"
#include "src/ml_ops/batch_matrix_multiply.h"
#include "src/ml_ops/fully_connected.h"
#include "src/ml_ops/ml_common.h"
#include "src/ml_ops/normalization.h"
#include "src/ml_ops/rope_weights.h"
#include "src/ml_ops/softmax.h"

namespace hallmark {

namespace {

using namespace Halide;

Var b{"b"}, t{"t"}, n{"n"}, h{"h"}, s{"s"};
// TODO(zalman): Ugly global.
Type generating_type;

enum class AttentionScaleType {
    PerDimScale,
    InverseSqrtHeadDim,
};

// TODO: Should be moved into Halide proper once sorted.
Expr fast_tanh(Expr x) {
    // In theory, this should be a really good approximation for tanh;
    // in practice, even very small (< 1e-7) differences in the result
    // can have profound impact on correctness of output. TODO: consider
    // adapting XNNPACK's approximation(s)?
    //
    // Expr r = (fast_exp(2*x)-1.f)/(fast_exp(2*x)+1.f);

    return tanh(x);
}

const std::map<std::string, AttentionScaleType> attention_scale_names = {
    {"per_dim_scale", AttentionScaleType::PerDimScale},
    {"inverse_sqrt_head_dim", AttentionScaleType::InverseSqrtHeadDim}};

Func soft_plus(Func weights, Expr dims_norm) {
    Expr scale = 1.442695041f / dims_norm;
    Func soft_plus("soft_plus");
    soft_plus(_) = (halide_log(1 + default_exp(cast<float>(-abs(weights(_))))) +
                    max(weights(_), 0.0f)) *
                   scale;
    return soft_plus;
}

Func gelu(Func in) {
    constexpr float sqrt_2_over_pi = 0.7978845608f;
    Expr elem = in(_);
    Func gelu_result("gelu_result");

    // Based on approximation from e.g: https://arxiv.org/pdf/1606.08415.pdf
    gelu_result(_) =
        elem *
        ((fast_tanh(((1 + (elem * elem * 0.044715f)) * elem) * sqrt_2_over_pi) +
          1) *
         .5f);
    return gelu_result;
}

// TODO: Optimize, make match xnnpack.
Func silu(Func in) {
    Func silu_result("silu_result");
    silu_result(_) = in(_) / (1 + default_exp(in(_)));
    return silu_result;
}

Func relu(Func in) {
    Func relu_result("relu_result");
    relu_result(_) = max(0.0f, in(_));
    return relu_result;
}

class LlmRoPEValues : public Halide::Generator<LlmRoPEValues> {
public:
    GeneratorParam<int32_t> head_dim_H_{"head_dim_H", 128};

    GeneratorParam<Type> processing_type_{"processing_type", Float(32)};

    Output<Buffer<void, 2>> segment_pos_values_{"segment_pos_values"};

    void configure() {
        segment_pos_values_.set_type(processing_type_);
    }

    void generate() {
        segment_pos_weights_.apply(head_dim_H_, processing_type_);
        segment_pos_values_ = segment_pos_weights_.result;
    }

    void schedule() {
        // TODO: apply static bounds.
        segment_pos_weights_.default_schedule(LoopLevel::root(), get_target());
    }

    RoPEWeights segment_pos_weights_{"segment_pos_weights"};  // @@@
};

class LlmPreprocessor : public Halide::Generator<LlmPreprocessor> {
public:
    GeneratorParam<int32_t> model_dim_D_{"model_dim_D", 2048};

    GeneratorParam<bool> skip_absolute_positional_embeddings_{
        "skip_absolute_positional_embeddings", true};

    GeneratorParam<Type> processing_type_{"processing_type", Float(32)};

    Input<Buffer<void, 3>> input_{"input"};
    // Optional input pos_embedding
    Input<Halide::Buffer<>> *pos_embedding_;

    Output<Buffer<void, 3>> scaled_embedding_{"scaled_embedding"};

    void configure() {
        input_.set_type(processing_type_);
        if (!skip_absolute_positional_embeddings_) {
            pos_embedding_ =
                add_input<Buffer<>>("pos_embeddings", processing_type_, 3);
        }
        scaled_embedding_.set_type(processing_type_);
    }

    void generate() {
        scaled_embedding_(n, t, b) =
            input_(n, t, b) * std::sqrt((float)model_dim_D_) +
            (skip_absolute_positional_embeddings_ ? 0 : (*pos_embedding_)(n, t, b));
    }

    void schedule() {
        // TODO: apply static bounds.
        scaled_embedding_.compute_root().vectorize(n, natural_vector_size<float>());
    }
};

// TODO: What should these kinds/modes really be called?
enum class TransformerKind {
    PrefixOnlyUncached,
    PrefixDecodeUpdateCache,
    PrefixDecodeUseCache,
};

inline const std::map<std::string, TransformerKind> transformer_kind_names = {
    {"prefix_only_uncached", TransformerKind::PrefixOnlyUncached},
    {"prefix_decode_update_cache", TransformerKind::PrefixDecodeUpdateCache},
    {"prefix_decode_use_cache", TransformerKind::PrefixDecodeUseCache}};

class LlmTransformer : public Halide::Generator<LlmTransformer> {
public:
    GeneratorParam<int32_t> batch_size_B_{"batch_size_B", 1};
    GeneratorParam<int32_t> seq_size_T_{"seq_size_T", 512};
    GeneratorParam<int32_t> model_dim_D_{"model_dim_D", 128};
    GeneratorParam<int32_t> hidden_dim_HD_{"hidden_dim_HD", 128};
    GeneratorParam<int32_t> head_dim_H_{"head_dim_H", 128};
    GeneratorParam<int32_t> n_heads_N_{"n_heads_N", 8};
    GeneratorParam<int32_t> voc_size_V_{"voc_size_V", 128};
    GeneratorParam<int32_t> num_kv_heads_{"num_kv_heads", 1};
    GeneratorParam<TransformerKind> transformer_kind_{
        "transformer_kind", TransformerKind::PrefixOnlyUncached,
        transformer_kind_names};

    GeneratorParam<Type> processing_type_{"processing_type", Float(32)};
    GeneratorParam<NormalizationKind> sa_pre_norm_{
        "sa_pre_norm", NormalizationKind::RMS, normalization_kind_names};
    GeneratorParam<NormalizationKind> sa_post_norm_{
        "sa_post_norm", NormalizationKind::RMS, normalization_kind_names};
    GeneratorParam<NormalizationKind> feed_forward_pre_norm_{
        "feedforward_pre_norm", NormalizationKind::RMS, normalization_kind_names};
    GeneratorParam<NormalizationKind> feed_forward_post_norm_{
        "feedforward_post_norm", NormalizationKind::RMS,
        normalization_kind_names};

    GeneratorParam<AttentionScaleType> attention_scale_type_{
        "attention_scale_type", AttentionScaleType::InverseSqrtHeadDim,
        attention_scale_names};

    GeneratorParam<bool> use_mqa_{"use_mqa", false};

    // TODO: Does this need to be made an input?
    GeneratorParam<float> soft_cap_{"soft_cap", 0.0f};

    GeneratorParam<Activation> feed_forward_params_activation_{
        "feed_forward_params_activation", Activation::RELU, activation_names};

    Input<Buffer<>> layer_input_{"layer_input", 3};
    Input<Buffer<>> segment_pos_values_{"segment_pos_values", 2};
    Input<Buffer<>> attention_mask_{"attention_mask", 2};

    // Only used for PrefixDecodeUpdateCache
    Output<Buffer<>> *key_cache_slice_output_;
    Output<Buffer<>> *value_cache_slice_output_;

    // Only used for PrefixDecodeUseCache
    Input<Buffer<>> *key_cache_input_;
    Input<Buffer<>> *value_cache_input_;

    // Optional per_dim_scale only present is attention_scale_type is PerDimScale.
    Input<Buffer<>> *per_dim_scale_;

    Output<Buffer<>> *layer_output_;

    void configure() {
        generating_type = processing_type_;
        layer_input_.set_type(processing_type_);
        segment_pos_values_.set_type(processing_type_);
        // TODO: handle layer norm args?
        pre_normed_.add_inputs(sa_pre_norm_, processing_type_, this);
        post_normed_.add_inputs(sa_post_norm_, processing_type_, this);

        // TODO: Parameterize quantization kind.
        // TODO: Find better convention for the "_3d" naming.
        key_proj_3d_.add_inputs(QuantizationKind::QC8NoBias, Int(8), model_dim_D_,
                                head_dim_H_, this);
        query_proj_3d_.add_inputs(QuantizationKind::QC8NoBias, Int(8), model_dim_D_,
                                  model_dim_D_, this);
        value_proj_3d_.add_inputs(QuantizationKind::QC8NoBias, Int(8), model_dim_D_,
                                  head_dim_H_, this);

        attention_mask_.set_type(processing_type_);
        post_attention_proj_.add_inputs(QuantizationKind::QC8NoBias, Int(8),
                                        model_dim_D_, model_dim_D_, this);
        if (transformer_kind_ != TransformerKind::PrefixDecodeUpdateCache) {
            final_pre_normed_.add_inputs(feed_forward_pre_norm_, processing_type_,
                                         this);
            final_post_normed_.add_inputs(feed_forward_post_norm_, processing_type_,
                                          this);
            // TODO: is the order of model and hidden dims correct?
            feed_forward_layer_1_.add_inputs(QuantizationKind::QC8NoBias, Int(8),
                                             model_dim_D_, hidden_dim_HD_, this);
            feed_forward_layer_1_gate_.add_inputs(QuantizationKind::QC8NoBias, Int(8),
                                                  model_dim_D_, hidden_dim_HD_, this);
            feed_forward_layer_2_.add_inputs(QuantizationKind::QC8NoBias, Int(8),
                                             hidden_dim_HD_, model_dim_D_, this);
            layer_output_ = add_output<Buffer<>>("layer_output", processing_type_, 3);
        }

        if (attention_scale_type_ == AttentionScaleType::PerDimScale) {
            per_dim_scale_ =
                add_input<Buffer<>>("per_dim_scale", processing_type_, 1);
        }

        if (transformer_kind_ == TransformerKind::PrefixDecodeUpdateCache) {
            // These are N, H, T, B or S, H, T, B (Halide ordering)
            key_cache_slice_output_ =
                add_output<Buffer<>>("key_cache_slice_output", processing_type_, 4);
            value_cache_slice_output_ =
                add_output<Buffer<>>("value_cache_slice_output", processing_type_, 4);
        }
        if (transformer_kind_ == TransformerKind::PrefixDecodeUseCache) {
            key_cache_input_ = add_input<Buffer<>>("key_cache", processing_type_, 4);
            value_cache_input_ =
                add_input<Buffer<>>("value_cache", processing_type_, 4);
        }
    }

    void generate() {
        Func input("input");
        // Name dimensions of input
        input(n, t, b) = layer_input_(n, t, b);
        pre_normed_.apply(input, model_dim_D_);

        key_proj_3d_.apply(pre_normed_.result, get_target());
        query_proj_3d_.apply(pre_normed_.result, get_target());
        value_proj_3d_.apply(pre_normed_.result, get_target());

        // TODO: The splits here may be computed from generator params that just
        // happen to work for this case based on the passed in key/query/value
        // projection weight sizes. Should probably introduce checks to make sure
        // the weights have these sizes or introduce new generator parameters.
        // It is possible to make these dynamic from the extents of the inputs, but
        // it may be expensive.
        //
        // Converts B,T,NH -> B,T,N,H or B,T,NH -> B,T,S,H
        //
        // Note, in original code, the split divisor here comes from
        // kKeySelfAttentionReshapedWeight in metadata.
        // The numerator of the split could be dim(0).extent from the weights, but
        // the generator param is a constant.

        Expr query_scale;
        if (attention_scale_type_ == AttentionScaleType::PerDimScale) {
            // TODO: memoize this.
            Func per_dim_scale_cached("per_dim_scale_cached");
            per_dim_scale_cached(h) = soft_plus(*per_dim_scale_, head_dim_H_)(h);
            query_scale = per_dim_scale_cached(h);
        } else if (attention_scale_type_ ==
                   AttentionScaleType::InverseSqrtHeadDim) {
            query_scale = fast_inverse_sqrt(cast(processing_type_, head_dim_H_));
        }

        int key_value_split =
            static_cast<int32_t>(head_dim_H_) / static_cast<int32_t>(num_kv_heads_);
        key_proj_4d_(s, n, t, b) =
            key_proj_3d_.result(s + n * key_value_split, t, b);
        value_proj_4d_(s, n, t, b) =
            value_proj_3d_.result(s + n * key_value_split, t, b);
        int query_split =
            static_cast<int32_t>(model_dim_D_) / static_cast<int32_t>(n_heads_N_);
        query_proj_4d_(h, n, t, b) =
            query_proj_3d_.result(h + n * query_split, t, b);

        CHECK(key_value_split == query_split);

        roped_key_proj_4d_.apply(key_proj_4d_, segment_pos_values_, head_dim_H_);
        roped_query_proj_4d_.apply(query_proj_4d_, segment_pos_values_,
                                   head_dim_H_);

        if (transformer_kind_ == TransformerKind::PrefixDecodeUpdateCache) {
            (*key_cache_slice_output_)(s, n, t, b) =
                roped_key_proj_4d_.result(s, n, t, b);
            (*value_cache_slice_output_)(s, n, t, b) = value_proj_4d_(s, n, t, b);
        } else {
            Func roped_key_proj_4d_switch("roped_key_proj_4d_switch");
            Func value_proj_4d_switch("value_proj_4d_switch");
            if (transformer_kind_ == TransformerKind::PrefixOnlyUncached) {
                roped_key_proj_4d_switch = roped_key_proj_4d_.result;
                value_proj_4d_switch = value_proj_4d_;
            } else {
                roped_key_proj_4d_switch = *key_cache_input_;
                value_proj_4d_switch = *value_cache_input_;
            }

            // Swap middle dimensions for key and query. BTN{H,S} -> BNT{S,H}
            key_proj_permuted_(s, t, n, b) = roped_key_proj_4d_switch(s, n, t, b);
            // BTNS -> BNST
            value_proj_permuted_(t, s, n, b) = value_proj_4d_switch(s, n, t, b);
            query_proj_permuted_(h, t, n, b) =
                roped_query_proj_4d_.result(h, n, t, b) * query_scale;

            // "maybe" because I'm not 100% sure this is what this input means.  Also
            // not 100% sure it is the thing to use where it's being used, but I think
            // so.
            // TODO: These should probably be taken from the output.
            Expr input_seq_len_maybe = layer_input_.dim(1).extent();
            Expr total_seq_len =
                layer_input_.dim(1).min() + layer_input_.dim(1).extent();

            Func logits("logits");
            if (use_mqa_) {
                // reshape key_permuted {0, llm_params_.head_dim_H}
                Func key_proj_permuted_reshaped("key_proj_permuted_reshaped");
                // TODO: Figure out the best way to do this.
                key_proj_permuted_reshaped = key_proj_permuted_;
                logits_fc_ = FullyConnected::float32_layer(query_proj_permuted_,
                                                           key_proj_permuted_reshaped,
                                                           head_dim_H_, model_dim_D_, get_target());
                logits = logits_fc_.result;
            } else {
                Func broadcast_key_proj_permuted("broadcast_key_proj_permuted");
                broadcast_key_proj_permuted(s, t, n, b) =
                    key_proj_permuted_(s, t, 0, b);
                Func transposed_key_proj_permuted("transposed_key_proj_permuted");
                transposed_key_proj_permuted(t, s, n, b) =
                    broadcast_key_proj_permuted(s, t, n, b);
                logits_bmm_.float32_layer(query_proj_permuted_,
                                          transposed_key_proj_permuted, key_value_split,
                                          input_seq_len_maybe, total_seq_len);
                logits = logits_bmm_.result;
            }

            // BNTS
            if (soft_cap_ > 0.0f) {
                logits(s, t, n, b) =
                    fast_tanh(logits(s, t, n, b) / soft_cap_) * soft_cap_;
            }
            Func padded_logits("padded_logits");
            padded_logits(s, t, n, b) = logits(s, t, n, b) + attention_mask_(s, t);

            // TODO: is size for this softmax correct?
            probs_softmax_.apply(padded_logits, total_seq_len, generating_type);

            Func broadcast_value_proj_permuted("broadcast_value_proj_permuted");
            broadcast_value_proj_permuted(s, t, n, b) =
                value_proj_permuted_(s, t, 0, b);
            Func transposed_value_proj_permuted("transposed_value_proj_permuted");
            transposed_value_proj_permuted(t, s, n, b) =
                broadcast_value_proj_permuted(s, t, n, b);
            outcome_before_permute_bmm_.float32_layer(
                probs_softmax_.result, transposed_value_proj_permuted, total_seq_len,
                input_seq_len_maybe, head_dim_H_);
            // Swap middle two dimensions back.
            kqv_merged_(h, n, t, b) = outcome_before_permute_bmm_.result(h, t, n, b);

            // Merge h and n dimensions.
            outcome_reshaped_(n, t, b) =
                kqv_merged_(n % head_dim_H_, n / head_dim_H_, t, b);
            post_attention_proj_.apply(outcome_reshaped_, get_target());

            post_normed_.apply(post_attention_proj_.result, model_dim_D_);

            // Rename for now to match use in calling function.
            Func output("output");
            output(n, t, b) = post_normed_.result(n, t, b) + input(n, t, b);

            final_pre_normed_.apply(output, model_dim_D_);
            feed_forward_layer_1_.apply(final_pre_normed_.result, get_target());
            feed_forward_layer_1_gate_.apply(final_pre_normed_.result, get_target());

            if (feed_forward_params_activation_ == Activation::None) {
                feed_forward_gate_(n, t, b) =
                    feed_forward_layer_1_gate_.result(n, t, b);
            } else if (feed_forward_params_activation_ == Activation::GELU) {
                feed_forward_gate_(n, t, b) =
                    gelu(feed_forward_layer_1_gate_.result)(n, t, b);
            } else if (feed_forward_params_activation_ == Activation::SILU) {
                feed_forward_gate_(n, t, b) =
                    silu(feed_forward_layer_1_gate_.result)(n, t, b);
            } else if (feed_forward_params_activation_ == Activation::RELU) {
                feed_forward_gate_(n, t, b) =
                    relu(feed_forward_layer_1_gate_.result)(n, t, b);
            } else {
                std::abort();
            }

            feed_forward_layer_1_and_gate_(n, t, b) =
                feed_forward_layer_1_.result(n, t, b) * feed_forward_gate_(n, t, b);

            feed_forward_layer_2_.apply(feed_forward_layer_1_and_gate_, get_target());

            final_post_normed_.apply(feed_forward_layer_2_.result, model_dim_D_);

            if (transformer_kind_ != TransformerKind::PrefixDecodeUpdateCache) {
                (*layer_output_)(n, t, b) =
                    final_post_normed_.result(n, t, b) + output(n, t, b);
            }
        }
    }

    using LL = LoopLevel;

    void schedule() {
#if 0
    root_schedule();
#else
        pre_normed_.default_schedule(LL::root(), target);

        // t and b are unbounded but n is always exactly 2048
        layer_input_.dim(0).set_extent(2048);

        if (transformer_kind_ == TransformerKind::PrefixDecodeUpdateCache) {
            key_proj_3d_.default_schedule(LL(roped_key_proj_4d_.inner, t), target);
            key_proj_4d_.compute_inline();
            key_proj_permuted_.compute_inline();

            query_proj_3d_.default_schedule(LL(roped_query_proj_4d_.inner, t),
                                            target);
            query_proj_4d_.compute_inline();
            query_proj_permuted_.compute_inline();

            value_proj_3d_.default_schedule(LL::root(), target);
            value_proj_4d_.compute_inline();
            value_proj_permuted_.compute_inline();

            roped_query_proj_4d_.default_schedule(LL(*key_cache_slice_output_, t),
                                                  target);
            roped_key_proj_4d_.default_schedule(LL(*key_cache_slice_output_, t),
                                                target);
        } else {
            auto &layer_output = (*layer_output_);
            key_proj_3d_.default_schedule(LL(roped_key_proj_4d_.inner, t), target);
            key_proj_4d_.compute_inline();
            key_proj_permuted_.compute_inline();

            query_proj_3d_.default_schedule(LL(probs_softmax_.result, b), target);
            query_proj_4d_.compute_inline();
            query_proj_permuted_.compute_inline();

            value_proj_3d_.default_schedule(LL::root(), target);
            value_proj_4d_.compute_inline();
            value_proj_permuted_.compute_inline();

            if (use_mqa_) {
                logits_fc_.default_schedule(LL(probs_softmax_.result, b), target);
            } else {
                const int parallel_split = 16;
                logits_bmm_.default_schedule(LL(probs_softmax_.result, b), target,
                                             parallel_split);
            }

            const bool vectorize_softmax =
                transformer_kind_ == TransformerKind::PrefixOnlyUncached;
            probs_softmax_.default_schedule(LL::root(), target, vectorize_softmax);

            const int parallel_split = 16;
            outcome_before_permute_bmm_.default_schedule(
                LL(post_attention_proj_.result, b), target, parallel_split);
            kqv_merged_.compute_inline();
            outcome_reshaped_.compute_inline();

            post_attention_proj_.default_schedule(LL::root(), target);
            post_normed_.default_schedule(LL::root(), target);

            roped_query_proj_4d_.default_schedule(LL(logits_bmm_.result, b), target);
            roped_key_proj_4d_.default_schedule(LL(logits_bmm_.result, b), target);
            final_pre_normed_.default_schedule(LL::root(), target);
            feed_forward_layer_1_.default_schedule(LL(feed_forward_layer_2_.result, b), target);
            feed_forward_layer_1_.result.hoist_storage_root();
            feed_forward_layer_1_gate_.default_schedule(LL(feed_forward_layer_2_.result, b), target);
            feed_forward_layer_1_gate_.result.hoist_storage_root();
            feed_forward_layer_1_and_gate_.compute_at(feed_forward_layer_2_.result, b)
                .hoist_storage_root();
            feed_forward_layer_2_.default_schedule(LL(layer_output, b), target);
            final_post_normed_.default_schedule(LL(layer_output, b), target);

            layer_output.dim(0).set_extent(2048);
            for (int d = 0; d < 3; d++) {
                layer_input_.dim(d).set_extent(layer_output.dim(d).extent());
            }
        }
#endif
    }

    void root_schedule() {
        pre_normed_.default_schedule(LL::root(), target);

        kqv_merged_.compute_root();

        key_proj_3d_.default_schedule(LL::root(), target);
        query_proj_3d_.default_schedule(LL::root(), target);
        // query_proj_3d_.result.debug_to_file("/tmp/qp3d.npy");
        value_proj_3d_.default_schedule(LL::root(), target);

        roped_key_proj_4d_.default_schedule(LL::root(), target);
        roped_query_proj_4d_.default_schedule(LL::root(), target);

        if (transformer_kind_ != TransformerKind::PrefixDecodeUpdateCache) {
            logits_bmm_.default_schedule(LL::root(), target, /*parallel_split*/ 0);
            probs_softmax_.default_schedule(LL::root(), target, /*vectorize*/ false);

            outcome_before_permute_bmm_.default_schedule(LL::root(), target,
                                                         /*parallel_split*/ 0);

            post_attention_proj_.default_schedule(LL::root(), target);
            post_normed_.default_schedule(LL::root(), target);

            final_pre_normed_.default_schedule(LL::root(), target);
            // Can these two be compute_with?
            feed_forward_layer_1_.default_schedule(LL::root(), target);
            feed_forward_layer_1_gate_.default_schedule(LL::root(), target);
            feed_forward_layer_2_.default_schedule(LL::root(), target);
            final_post_normed_.default_schedule(LL::root(), target);
        }
    }

    Input<Buffer<>> *sa_pre_norm_weights_{};
    Input<Buffer<>> *sa_post_norm_weights_{};
    Input<Buffer<>> *feed_forward_pre_norm_weights_{};
    Input<Buffer<>> *feed_forward_post_norm_weights_{};

    Normalization pre_normed_{"pre_normed"};

    FullyConnected key_proj_3d_{"key_proj_3d"};
    FullyConnected query_proj_3d_{"query_proj_3d"};
    FullyConnected value_proj_3d_{"value_proj_3d"};

    Func key_proj_4d_{"key_proj_4d"};
    Func query_proj_4d_{"query_proj_4d"};
    Func value_proj_4d_{"value_proj_4d"};

    RoPE roped_key_proj_4d_{"roped_key_proj_4d"};
    RoPE roped_query_proj_4d_{"roped_query_proj_4d"};

    Func query_proj_permuted_{"query_proj_permuted"};
    Func key_proj_permuted_{"key_proj_permuted"};
    Func value_proj_permuted_{"value_proj_permuted"};

    FullyConnected logits_fc_{"logits"};
    BatchMatrixMultiply logits_bmm_{"logits"};

    Normalization post_normed_{"post_normed"};

    Softmax probs_softmax_{"probs_softmax"};

    BatchMatrixMultiply outcome_before_permute_bmm_{"outcome_before_permute"};

    Func kqv_merged_{"kqv_merged"};
    Func outcome_reshaped_{"outcome_reshaped"};
    FullyConnected post_attention_proj_{"post_attention_proj"};

    FullyConnected feed_forward_layer_1_{"feed_forward_layer_1"};
    Func feed_forward_gate_{"feed_forward_gate"};
    FullyConnected feed_forward_layer_1_gate_{"feed_forward_layer_1_gate"};
    Func feed_forward_layer_1_and_gate_{"feed_forward_layer_1_and_gate"};
    FullyConnected feed_forward_layer_2_{"feed_forward_layer_2"};

    Normalization final_pre_normed_{"final_pre_normed"};
    Normalization final_post_normed_{"final_post_normed"};
};

class LlmPostprocessor : public Halide::Generator<LlmPostprocessor> {
public:
    GeneratorParam<uint32_t> batch_size_B_{"batch_size_B", 1};
    GeneratorParam<uint32_t> seq_size_T_{"seq_size_T", 512};
    GeneratorParam<uint32_t> model_dim_D_{"model_dim_D", 128};
    GeneratorParam<uint32_t> head_dim_H_{"head_dim_H", 128};
    GeneratorParam<uint32_t> voc_size_V_{"voc_size_V", 128};

    GeneratorParam<NormalizationKind> final_norm_{
        "final_norm", NormalizationKind::RMS, normalization_kind_names};

    GeneratorParam<Type> processing_type_{"processing_type", Float(32)};

    // Inputs are last transformer layer output, final_norm,
    // final_post_process_weights
    Input<Buffer<>> layer_input_{"layer_input", 3};

    Output<Buffer<>> result_{"result", 3};

    void configure() {
        generating_type = processing_type_;
        layer_input_.set_type(processing_type_);
        post_process_normed_.add_inputs(final_norm_, processing_type_, this);

        feed_forward_.add_inputs(QuantizationKind::QC8NoBias, Int(8), model_dim_D_,
                                 voc_size_V_, this);

        result_.set_type(processing_type_);
    }

    void generate() {
        // Gives var names to arguments, which are used in operators.
        Func postprocess_input("postprocess_input");
        postprocess_input(n, t, b) = layer_input_(n, t, b);
        // TODO: is size right for normalization here?
        post_process_normed_.apply(postprocess_input, head_dim_H_);
        // TODO: Anything to do to ensure softmax linear?
        feed_forward_.apply(post_process_normed_.result, get_target());

        result_ = feed_forward_.result;
    }

    void schedule() {
        post_process_normed_.default_schedule(LoopLevel(feed_forward_.result, b),
                                              get_target());
        feed_forward_.default_schedule(LoopLevel(feed_forward_.result, t),
                                       get_target());
    }

    Normalization post_process_normed_{"post_process_normed"};
    FullyConnected feed_forward_{"feed_forward"};
};

class LlmPositionEmbedding : public Halide::Generator<LlmPositionEmbedding> {
public:
    Input<int32_t> input_length_{"input_length"};
    Input<int32_t> seq_length_{"seq_length"};
    Input<int32_t> embedding_dim_{"embedding_dim"};
    Input<float> min_timescale_{"min_timescale"};
    Input<float> max_timescale_{"max_timescale"};

    Output<Buffer<float>> result_{"result", 2};

    void generate() {
        input_range_ = RDom(0, embedding_dim_ / 2, 0, input_length_);
        seq_range_ =
            RDom(0, embedding_dim_ / 2, input_length_, seq_length_ - input_length_);
        Expr log_timescale_inc = default_log(max_timescale_ / min_timescale_) /
                                 max(embedding_dim_ / 2.0f, 1.0f);
        Expr inv_timescale =
            min_timescale_ * default_exp(input_range_.x * log_timescale_inc);

        result_(n, h) = undef<float>();
        result_(input_range_.x, input_range_.y) =
            select(input_range_.x > embedding_dim_ / 2,
                   fast_cos(input_range_.y * inv_timescale),
                   fast_sin(input_range_.y * inv_timescale));
        result_(seq_range_.x, seq_range_.y) =
            select(seq_range_.x > embedding_dim_ / 2, 0.f, 1.f);
    }

    void schedule() {
        // Turning this on causes a Halide compilation error complaining about
        // a redundant update definition.
        // Schedule
        RVar ro("ro"), ri("ri");
        result_.compute_root();
        result_.update(0)
            .split(input_range_.x, ro, ri, embedding_dim_ / 2)
            .unroll(ro)
            .vectorize(ri, natural_vector_size<float>());
        result_.update(1)
            .split(seq_range_.x, ro, ri, embedding_dim_ / 2)
            .unroll(ro)
            .vectorize(ri, natural_vector_size<float>());
    }

    RDom input_range_;
    RDom seq_range_;
};

}  // namespace

}  // namespace hallmark

HALIDE_REGISTER_GENERATOR(hallmark::LlmRoPEValues, LlmRoPEValues);
HALIDE_REGISTER_GENERATOR(hallmark::LlmPreprocessor,
                          LlmPreprocessor);
HALIDE_REGISTER_GENERATOR(hallmark::LlmTransformer, LlmTransformer);
HALIDE_REGISTER_GENERATOR(hallmark::LlmPostprocessor,
                          LlmPostprocessor);
HALIDE_REGISTER_GENERATOR(hallmark::LlmPositionEmbedding,
                          LlmPositionEmbedding);
