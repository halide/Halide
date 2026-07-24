find_package(Halide REQUIRED COMPONENTS static JIT)

get_target_property(multi_dir_actual_type Halide::Halide TYPE)
if (NOT multi_dir_actual_type STREQUAL "STATIC_LIBRARY")
    message(
        FATAL_ERROR "static_consumer expected Halide::Halide to be STATIC_LIBRARY, got ${multi_dir_actual_type}"
    )
endif ()
unset(multi_dir_actual_type)

add_executable(static_consumer_main "${CMAKE_CURRENT_SOURCE_DIR}/../main.cpp")
target_link_libraries(static_consumer_main PRIVATE Halide::Halide)

add_test(NAME static_consumer_validate COMMAND static_consumer_main)
set_tests_properties(static_consumer_validate PROPERTIES PASS_REGULAR_EXPRESSION "Success!")
