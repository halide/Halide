find_package(GTest REQUIRED)

# Google Test based warning tests
add_executable(
        warning_tests
        hidden_pure_definition.cpp
        require_const_false.cpp
        sliding_vectors.cpp
        unscheduled_update_def.cpp
        emulated_float16.cpp
)

target_link_libraries(warning_tests
        PRIVATE
        Halide::Test
        GTest::gtest_main
)

gtest_discover_tests(warning_tests)
