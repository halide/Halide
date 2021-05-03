cmake_minimum_required(VERSION 3.16)
project(hannk)

enable_testing()

# -fPIC is necessary for .so builds (at least on Linux); not necessary for the non-delegate
# builds but easier to enable it for everything.
add_compile_options(-fPIC)

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Set up the version of TFLite we expect
set(TFLITE_VERSION_MAJOR "2" CACHE STRING "Major version of TFLite to assume")
set(TFLITE_VERSION_MINOR "5" CACHE STRING "Minor version of TFLite to assume")
set(TFLITE_VERSION_PATCH "0-rc2" CACHE STRING "Patch version of TFLite to assume")

set(TFLITE_VERSION "${TFLITE_VERSION_MAJOR}.${TFLITE_VERSION_MINOR}.${TFLITE_VERSION_PATCH}")

add_subdirectory(delegate)
add_subdirectory(halide)
add_subdirectory(interpreter)
add_subdirectory(tflite)
add_subdirectory(util)

# Benchmarking executable
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE
                      tflite_parser
                      interpreter
                      error_util
                      file_util
                      hannk_log_stderr
                      Halide::Tools  # for halide_benchmark.h
                      Halide::Runtime)

add_executable(compare_vs_tflite compare_vs_tflite.cpp)
target_compile_definitions(compare_vs_tflite PRIVATE
                           -DTFLITE_VERSION_MAJOR=${TFLITE_VERSION_MAJOR}
                           -DTFLITE_VERSION_MINOR=${TFLITE_VERSION_MINOR}
                           -DTFLITE_VERSION_PATCH=${TFLITE_VERSION_PATCH}
                           -DHANNK_USE_REGISTRAR_DELEGATE)
target_link_libraries(compare_vs_tflite PRIVATE
                      tensorflowlite_c
                      tflite_parser
                      interpreter
                      error_util
                      file_util
                      hannk_log_stderr
                      hannk_delegate_registrar
                      Halide::Tools  # for halide_benchmark.h
                      Halide::Runtime)
target_include_directories(compare_vs_tflite
                           PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

