cmake_minimum_required(VERSION 3.16)
project(random_pipeline)

# Generator
add_executable(random_pipeline.generator random_pipeline_generator.cpp)
target_link_libraries(random_pipeline.generator PRIVATE Halide::Generator)

# Filters
add_halide_library(random_pipeline FROM random_pipeline.generator)

# Final executable
add_executable(random_pipeline_test test.cpp)
target_link_libraries(random_pipeline_test PUBLIC random_pipeline)
target_include_directories(random_pipeline_test PRIVATE "${CMAKE_SOURCE_DIR}/tools")

if (NOT MSVC)
  target_compile_options(random_pipeline_test PRIVATE "-O2")
  if (OPENMP_FOUND)
    target_compile_options(random_pipeline_test PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(random_pipeline_test PRIVATE ${OpenMP_CXX_FLAGS})
  else()
    target_compile_options(random_pipeline_test PRIVATE "-Wno-unknown-pragmas")
  endif()
endif()
