add_executable(llm_generator_test llm_generator_test.cc)
target_link_libraries(llm_generator_test
                      PRIVATE
                        absl::flags
                        benchmark::benchmark
                        GTest::gtest_main
                        hamal_position_embedding
                        hamal_postprocessor
                        hamal_preprocessor
                        hamal_rope_values
                        hamal_status_helpers
                        hamal_transformer_kv_update_cache
                        hamal_transformer_kv_use_cache
                        hamal_transformer_no_kv_cache)

include(GoogleTest)
gtest_discover_tests(llm_generator_test)

