if (WIN32)
    # Halide OpenGL is broken on Windows.
    return()
endif ()

cmake_minimum_required(VERSION 3.16)
project(glsl)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generators
add_executable(glsl_blur.generator halide_blur_glsl_generator.cpp)
target_link_libraries(glsl_blur.generator PRIVATE Halide::Generator)

add_executable(ycc.generator halide_ycc_glsl_generator.cpp)
target_link_libraries(ycc.generator PRIVATE Halide::Generator)

# Libraries
add_halide_library(halide_blur_glsl FROM glsl_blur.generator FEATURES opengl debug)
add_halide_library(halide_ycc_glsl FROM ycc.generator FEATURES opengl debug)

# Final executable
add_executable(opengl_test opengl_test.cpp)
target_link_libraries(opengl_test PRIVATE halide_blur_glsl halide_ycc_glsl)

add_test(NAME opengl_test COMMAND opengl_test)
