# Generator
halide_project(halide_blur "apps" halide_blur.cpp)
set(halide_blur_h "${CMAKE_CURRENT_BINARY_DIR}/halide_blur.h")
set(halide_blur_obj "${CMAKE_CURRENT_BINARY_DIR}/halide_blur.o")

# Final executable
# FIXME: Need to mimic the OpenMP, -O2 and -msse2 flags in the Makefile
add_executable(blur_test test.cpp ${halide_blur_h})
target_link_libraries(blur_test PRIVATE "${halide_blur_obj}")
target_include_directories(blur_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(blur_test PRIVATE dl pthread)
endif()
set_target_properties(blur_test halide_blur PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")

# FIXME: Cannot use halide_add_generator_dependency() because
# halide_blur doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${halide_blur_h}" "${halide_blur_obj}"
                   COMMAND halide_blur
                   COMMENT "Generating halide_blur"
                  )
