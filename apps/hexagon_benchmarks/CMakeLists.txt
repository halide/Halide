cmake_minimum_required(VERSION 3.22)
project(hexagon_benchmarks)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# Find Halide
find_package(Halide REQUIRED)

macro(add_generator_and_library FILTER_NAME)
    set(GENERATOR_EXE ${FILTER_NAME}.generator)
    set(GENERATOR_SRC ${FILTER_NAME}_generator.cpp)
    add_halide_generator(${GENERATOR_EXE} SOURCES ${GENERATOR_SRC})
    add_halide_library(${FILTER_NAME} FROM ${GENERATOR_EXE})
endmacro()

add_generator_and_library(dilate3x3)
add_generator_and_library(gaussian5x5)
add_generator_and_library(median3x3)

# Main executable
add_executable(process process.cpp)
target_compile_options(process PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2>)
if (Halide_TARGET MATCHES "hvx")
  target_compile_definitions(process PRIVATE DILATE3X3 GAUSSIAN5X5 MEDIAN3X3 TARGET_HAS_HVX)
else()
  target_compile_definitions(process PRIVATE DILATE3X3 GAUSSIAN5X5 MEDIAN3X3)
endif()
target_link_libraries(process
                      PRIVATE
                      Halide::Tools
                      dilate3x3 gaussian5x5 median3x3)

# Test that the app actually works!
add_test(NAME hexagon_benchmarks COMMAND process -n 1)
set_tests_properties(hexagon_benchmarks PROPERTIES
                     LABELS hexagon_benchmarks
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
