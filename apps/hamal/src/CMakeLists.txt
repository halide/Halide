add_subdirectory(ml_ops)

add_halide_generator(llm.generator llm_generator.cc)
target_link_libraries(llm.generator PRIVATE hamal_ml_ops)

add_halide_library(
    hamal_rope_values FROM llm.generator
    FUNCTION_NAME rope_values
    GENERATOR LlmRoPEValues
    PARAMS
        head_dim_H=256
        processing_type=float32
    NAMESPACE hamal
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hamal_preprocessor FROM llm.generator
    FUNCTION_NAME preprocessor
    GENERATOR LlmPreprocessor
    PARAMS
        model_dim_D=2048
        skip_absolute_positional_embeddings=true
        processing_type=float32
    NAMESPACE hamal
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hamal_transformer_no_kv_cache FROM llm.generator
    FUNCTION_NAME transformer_no_kv_cache
    GENERATOR LlmTransformer
    PARAMS
        seq_size_T=512
        model_dim_D=2048
        head_dim_H=256
        hidden_dim_HD=16384
        transformer_kind=prefix_only_uncached
        processing_type=float32
        sa_pre_norm=rms
        sa_post_norm=none
        feedforward_pre_norm=rms
        feedforward_post_norm=none
        attention_scale_type=inverse_sqrt_head_dim
        use_mqa=false
        soft_cap=0.0
        feed_forward_params_activation=gelu
    NAMESPACE hamal
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hamal_transformer_kv_update_cache FROM llm.generator
    FUNCTION_NAME transformer_kv_update_cache
    GENERATOR LlmTransformer
    PARAMS
        seq_size_T=512
        model_dim_D=2048
        hidden_dim_HD=16384
        head_dim_H=256
        transformer_kind=prefix_decode_update_cache
        processing_type=float32
        sa_pre_norm=rms
        sa_post_norm=none
        feedforward_pre_norm=rms
        feedforward_post_norm=none
        attention_scale_type=inverse_sqrt_head_dim
        use_mqa=false
        soft_cap=0.0
        feed_forward_params_activation=gelu
    NAMESPACE hamal
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hamal_transformer_kv_use_cache FROM llm.generator
    FUNCTION_NAME transformer_kv_use_cache
    GENERATOR LlmTransformer
    PARAMS
        seq_size_T=512
        model_dim_D=2048
        hidden_dim_HD=16384
        head_dim_H=256
        transformer_kind=prefix_decode_use_cache
        processing_type=float32
        sa_pre_norm=rms
        sa_post_norm=none
        feedforward_pre_norm=rms
        feedforward_post_norm=none
        attention_scale_type=inverse_sqrt_head_dim
        use_mqa=false
        soft_cap=0.0
        feed_forward_params_activation=gelu
    NAMESPACE hamal
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hamal_postprocessor FROM llm.generator
    FUNCTION_NAME postprocessor
    GENERATOR LlmPostprocessor
    PARAMS
        seq_size_T=512
        model_dim_D=2048
        head_dim_H=256
        voc_size_V=256000
    NAMESPACE hamal
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hamal_position_embedding FROM llm.generator
    FUNCTION_NAME position_embedding
    GENERATOR LlmPositionEmbedding
)

# --------------------

# Sigh, header-only libraries shouldn't be special
add_library(hamal_status_helpers INTERFACE)
target_include_directories(hamal_status_helpers INTERFACE
                           $<BUILD_INTERFACE:${hamal_SOURCE_DIR}>)

add_library(hamal_llm llm.cc)
target_link_libraries(hamal_llm
                      PRIVATE
                        absl::status
                        hamal_position_embedding
                        hamal_postprocessor
                        hamal_preprocessor
                        hamal_rope_values
                        hamal_status_helpers
                        hamal_transformer_kv_update_cache
                        hamal_transformer_kv_use_cache
                        hamal_transformer_no_kv_cache)
