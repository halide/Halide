cmake_minimum_required(VERSION 3.16)
project(wavelet)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(wavelet.generator
                     SOURCES
                     daubechies_x_generator.cpp
                     inverse_daubechies_x_generator.cpp
                     haar_x_generator.cpp
                     inverse_haar_x_generator.cpp)

# Filters
add_halide_library(daubechies_x FROM wavelet.generator)
add_halide_library(inverse_daubechies_x FROM wavelet.generator)
add_halide_library(haar_x FROM wavelet.generator)
add_halide_library(inverse_haar_x FROM wavelet.generator)

# Main executable
add_executable(wavelet wavelet.cpp)
target_link_libraries(wavelet
                      PRIVATE
                      Halide::Halide
                      Halide::ImageIO
                      daubechies_x
                      inverse_daubechies_x
                      haar_x
                      inverse_haar_x)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/gray.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} gray.png COPYONLY)
    add_test(NAME wavelet COMMAND wavelet gray.png .)
    set_tests_properties(wavelet PROPERTIES
                         LABELS wavelet
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
