include_directories ("${CMAKE_CURRENT_BINARY_DIR}")
  
halide_project(halide_blur "apps" 
  halide_blur.cpp)

add_custom_command(OUTPUT halide_blur.h
                   COMMAND halide_blur
                   DEPENDS halide_blur.cpp
                   COMMENT "Building halide_blur.h...")

halide_project(halide_blur_test "apps" 
  test.cpp halide_blur.h)

add_dependencies(halide_blur_test halide_blur)

target_link_libraries(
    halide_blur_test
    ${CMAKE_CURRENT_BINARY_DIR}/halide_blur.o
)
