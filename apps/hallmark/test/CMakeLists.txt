add_executable(llm_generator_test llm_generator_test.cc)
target_link_libraries(llm_generator_test
                      PRIVATE
                        absl::flags
                        absl::flags_parse
                        GTest::gtest
                        hallmark_contrib
                        hallmark_llm
                        hallmark_position_embedding
                        hallmark_postprocessor
                        hallmark_preprocessor
                        hallmark_rope_values
                        hallmark_transformer_kv_update_cache
                        hallmark_transformer_kv_use_cache
                        hallmark_transformer_no_kv_cache)

include(GoogleTest)
gtest_discover_tests(llm_generator_test)

add_executable(llm_generator_bench llm_generator_bench.cc)
target_link_libraries(llm_generator_bench
                      PRIVATE
                        absl::flags
                        absl::flags_parse
                        benchmark::benchmark
                        hallmark_contrib
                        hallmark_llm
                        hallmark_position_embedding
                        hallmark_postprocessor
                        hallmark_preprocessor
                        hallmark_rope_values
                        hallmark_transformer_kv_update_cache
                        hallmark_transformer_kv_use_cache
                        hallmark_transformer_no_kv_cache)
