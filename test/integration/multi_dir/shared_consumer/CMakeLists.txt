find_package(Halide REQUIRED COMPONENTS shared JIT)

get_target_property(multi_dir_actual_type Halide::Halide TYPE)
if (NOT multi_dir_actual_type STREQUAL "SHARED_LIBRARY")
    message(
        FATAL_ERROR "shared_consumer expected Halide::Halide to be SHARED_LIBRARY, got ${multi_dir_actual_type}"
    )
endif ()
unset(multi_dir_actual_type)

add_executable(shared_consumer_main "${CMAKE_CURRENT_SOURCE_DIR}/../main.cpp")
target_link_libraries(shared_consumer_main PRIVATE Halide::Halide)

add_test(NAME shared_consumer_validate COMMAND shared_consumer_main)
set_tests_properties(shared_consumer_validate PROPERTIES PASS_REGULAR_EXPRESSION "Success!")
