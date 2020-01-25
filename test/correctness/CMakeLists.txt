tests(correctness)

# Make sure the test that needs image_io has it
target_link_libraries(correctness_image_io PRIVATE Halide::ImageIO)

# Add the one C-language test manually
add_executable(correctness_plain_c_includes plain_c_includes.c)
target_include_directories(correctness_plain_c_includes PRIVATE "${HALIDE_INCLUDE_DIR}")
add_halide_test(correctness_plain_c_includes GROUPS correctness)