cmake_minimum_required(VERSION 3.16)
project(fft)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(fft.generator SOURCES fft_generator.cpp fft.cpp)

# Filters
add_halide_library(fft_forward_r2c FROM fft.generator
                   GENERATOR fft
                   PARAMS direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex)
add_halide_library(fft_inverse_c2r FROM fft.generator
                   GENERATOR fft
                   PARAMS direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real)
add_halide_library(fft_forward_c2c FROM fft.generator
                   GENERATOR fft
                   PARAMS direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex)
add_halide_library(fft_inverse_c2c FROM fft.generator
                   GENERATOR fft
                   PARAMS direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex)

# Main executable
add_executable(fft_aot_test fft_aot_test.cpp)
target_link_libraries(fft_aot_test PRIVATE Halide::ImageIO fft_forward_r2c fft_inverse_c2r fft_forward_c2c fft_inverse_c2c)

# Benchmarking executable
add_executable(bench_fft main.cpp fft.cpp)
target_link_libraries(bench_fft PRIVATE Halide::Halide Halide::Tools)

# Test that the app actually works!
add_test(NAME fft_aot_test COMMAND fft_aot_test)
set_tests_properties(fft_aot_test PROPERTIES
                     LABELS fft
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")

foreach (i IN ITEMS 8 12 16 24 32 48)
    add_test(NAME bench${i}x${i} COMMAND bench_fft ${i} ${i} "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(bench${i}x${i} 
                         PROPERTIES
                         LABELS fft
                         ENVIRONMENT "PATH=$<SHELL_PATH:$<TARGET_FILE_DIR:Halide::Halide>>")
endforeach ()
