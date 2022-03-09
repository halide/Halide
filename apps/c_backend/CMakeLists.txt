cmake_minimum_required(VERSION 3.16)
project(c_backend)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator(s)
add_halide_generator(pipeline.generator SOURCES pipeline_generator.cpp)

add_halide_library(pipeline_c FROM pipeline.generator
                   C_BACKEND
                   GENERATOR pipeline)
add_halide_library(pipeline_native FROM pipeline.generator
                   GENERATOR pipeline)

add_halide_generator(pipeline_cpp.generator SOURCES pipeline_cpp_generator.cpp)

add_halide_library(pipeline_cpp_cpp FROM pipeline_cpp.generator
                   C_BACKEND
                   GENERATOR pipeline_cpp
                   FEATURES c_plus_plus_name_mangling)
add_halide_library(pipeline_cpp_native FROM pipeline_cpp.generator
                   GENERATOR pipeline_cpp
                   FEATURES c_plus_plus_name_mangling)

# Final executable(s)
add_executable(run_c_backend_and_native run.cpp)
target_link_libraries(run_c_backend_and_native
                      PRIVATE
                      pipeline_native
                      pipeline_c)

add_executable(run_c_backend_and_native_cpp run_cpp.cpp)
target_link_libraries(run_c_backend_and_native_cpp
                      PRIVATE
                      pipeline_cpp_native
                      pipeline_cpp_cpp)

# Test that the app actually works!
add_test(NAME c_backend COMMAND run_c_backend_and_native)
add_test(NAME c_backend_cpp COMMAND run_c_backend_and_native_cpp)

set_tests_properties(c_backend c_backend_cpp PROPERTIES
                     LABELS c_backend
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
