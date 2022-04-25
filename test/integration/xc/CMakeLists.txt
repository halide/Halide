cmake_minimum_required(VERSION 3.16)
project(xc)

enable_testing()

# Only need the platform-independent / source-only helpers.
find_package(HalideHelpers REQUIRED)

add_halide_generator(add_gen add.cpp)
add_halide_library(add FROM xc::halide_generators::add_gen
                   REGISTRATION add_reg_cpp)

add_executable(run_add ${add_reg_cpp})
target_link_libraries(run_add PRIVATE add Halide::RunGenMain)

add_test(NAME run_add
         COMMAND run_add --output_extents=[10,10] --benchmarks=all)
