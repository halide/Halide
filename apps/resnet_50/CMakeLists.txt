cmake_minimum_required(VERSION 3.28)
project(resnet_50)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(resnet50.generator SOURCES Resnet50Generator.cpp)

# Filter
add_halide_library(resnet50 FROM resnet50.generator)

# Main executable
add_executable(resnet_50_process process.cpp)
target_link_libraries(resnet_50_process PRIVATE Halide::ImageIO Halide::Tools resnet50)

# NOTE: Like the Makefile, we only test that resnet_50 builds. Actually running
# process requires the PyTorch/torchvision weights produced by load_weights.py,
# which we don't want to pull in as a build/test dependency here.
