tests(
    GROUPS warning
    SOURCES
    metal_assertion_in_kernel.cpp
    require_const_false.cpp
    sliding_vectors.cpp
    unscheduled_update_def.cpp
)
set(WARNING_TESTS ${TEST_NAMES})

# See doc/Testing.md for the meaning of these groups.
tests(GROUPS warning target_independent llvm_independent SOURCES hidden_pure_definition.cpp)
list(APPEND WARNING_TESTS ${TEST_NAMES})

tests(
    GROUPS warning target_independent
    SOURCES emulated_float16.cpp unsupported_vectorization_sve.cpp
)
list(APPEND WARNING_TESTS ${TEST_NAMES})

# Don't look for "Success!" in warning tests, look for "Warning:" instead.
set_tests_properties(${WARNING_TESTS} PROPERTIES PASS_REGULAR_EXPRESSION "Warning:")
