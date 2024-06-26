cmake_minimum_required(VERSION 3.22)
project(jit)

enable_testing()

set(jit_HALIDE_VERSION ""
    CACHE STRING "Optional version argument to find_package(Halide)")

set(jit_HALIDE_COMPONENTS ""
    CACHE STRING "Optional required-components argument to find_package(Halide)")

find_package(Halide ${jit_HALIDE_VERSION} REQUIRED ${jit_HALIDE_COMPONENTS})

add_executable(main main.cpp)
target_link_libraries(main PRIVATE Halide::Halide)

add_test(NAME validate COMMAND main)

set_tests_properties(validate PROPERTIES PASS_REGULAR_EXPRESSION "Success!")
