cmake_minimum_required(VERSION 3.28)
project(flashattention)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)

add_executable(flash_attention flash_attention.cpp)
target_link_libraries(flash_attention PRIVATE Halide::Halide)

add_test(NAME flash_attention COMMAND flash_attention)
set_tests_properties(
    flash_attention
    PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS"
    FAIL_REGULAR_EXPRESSION "FAIL"
)
