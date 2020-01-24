tests(correctness)

# Make sure the test that needs image_io has it
halide_use_image_io(correctness_image_io)

# Add the one C-language test manually
add_executable(correctness_plain_c_includes plain_c_includes.c)
target_include_directories(correctness_plain_c_includes PRIVATE "${HALIDE_INCLUDE_DIR}")
add_halide_test(correctness_plain_c_includes GROUPS correctness)