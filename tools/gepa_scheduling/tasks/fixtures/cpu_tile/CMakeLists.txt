cmake_minimum_required(VERSION 3.28)
project(gepa_cpu_tile LANGUAGES CXX)

find_package(Halide REQUIRED)

add_executable(task main.cpp)
target_link_libraries(task PRIVATE Halide::Halide)
target_compile_features(task PRIVATE cxx_std_17)
