add_executable(test_halide_blas test_halide_blas.cpp)
target_link_libraries(test_halide_blas PRIVATE ${DEFAULT_BLAS} halide_blas)
add_test(NAME test_halide_blas COMMAND test_halide_blas)
set_tests_properties(test_halide_blas PROPERTIES
                     LABELS linear_algebra
                     PASS_REGULAR_EXPRESSION "Success!"
                     FAIL_REGULAR_EXPRESSION "FAILED"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
