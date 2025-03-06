add_subdirectory(ml_ops)

add_halide_generator(llm.generator llm_generator.cpp)
target_link_libraries(llm.generator PRIVATE hallmark_ml_ops absl::log)

add_halide_library(
    hallmark_rope_values FROM llm.generator
    FUNCTION_NAME rope_values
    GENERATOR LlmRoPEValues
    PARAMS
        head_dim_H=256
        processing_type=float32
    NAMESPACE hallmark
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hallmark_preprocessor FROM llm.generator
    FUNCTION_NAME preprocessor
    GENERATOR LlmPreprocessor
    PARAMS
        model_dim_D=2048
        skip_absolute_positional_embeddings=true
        processing_type=float32
    NAMESPACE hallmark
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hallmark_transformer_no_kv_cache FROM llm.generator
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
    NAMESPACE hallmark
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hallmark_transformer_kv_update_cache FROM llm.generator
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
    NAMESPACE hallmark
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hallmark_transformer_kv_use_cache FROM llm.generator
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
    NAMESPACE hallmark
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hallmark_postprocessor FROM llm.generator
    FUNCTION_NAME postprocessor
    GENERATOR LlmPostprocessor
    PARAMS
        seq_size_T=512
        model_dim_D=2048
        head_dim_H=256
        voc_size_V=256000
    NAMESPACE hallmark
    FEATURES c_plus_plus_name_mangling
)

add_halide_library(
    hallmark_position_embedding FROM llm.generator
    FUNCTION_NAME position_embedding
    GENERATOR LlmPositionEmbedding
)

# --------------------

add_library(hallmark_llm
            llm.cpp)
target_include_directories(hallmark_llm PRIVATE
                           $<BUILD_INTERFACE:${hallmark_SOURCE_DIR}>)
target_link_libraries(hallmark_llm
                      PRIVATE
                        absl::status
                        hallmark_contrib
                        hallmark_position_embedding
                        hallmark_postprocessor
                        hallmark_preprocessor
                        hallmark_rope_values
                        hallmark_transformer_kv_update_cache
                        hallmark_transformer_kv_use_cache
                        hallmark_transformer_no_kv_cache)

add_executable(llm_runner llm_runner.cpp)
target_link_libraries(llm_runner
                      PRIVATE
                        absl::flags
                        absl::flags_parse
                        hallmark_contrib
                        hallmark_llm
                        hallmark_position_embedding
                        hallmark_postprocessor
                        hallmark_preprocessor
                        hallmark_rope_values
                        hallmark_transformer_kv_update_cache
                        hallmark_transformer_kv_use_cache
                        sentencepiece)
target_include_directories(llm_runner PUBLIC ${sentencepiece_SOURCE_DIR})
