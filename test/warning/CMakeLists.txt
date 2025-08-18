find_package(GTest REQUIRED)

add_executable(warning_tests)
target_link_libraries(warning_tests PRIVATE Halide::Test GTest::gtest_main)

gtest_discover_tests(
    warning_tests
    PROPERTIES
    ENVIRONMENT "HL_TARGET=${Halide_TARGET};HL_JIT_TARGET=${Halide_TARGET}"
    LABELS "warning"
)

target_sources(
    warning_tests
    PRIVATE
    hidden_pure_definition.cpp
    require_const_false.cpp
    sliding_vectors.cpp
    unscheduled_update_def.cpp
    emulated_float16.cpp
)
