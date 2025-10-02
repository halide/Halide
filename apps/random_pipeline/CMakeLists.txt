cmake_minimum_required(VERSION 3.28)
project(random_pipeline)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(random_pipeline.generator
                     SOURCES random_pipeline_generator.cpp
                     LINK_LIBRARIES Halide::Tools)

# Filters
add_halide_library(random_pipeline FROM random_pipeline.generator)
add_halide_library(random_pipeline_auto_schedule FROM random_pipeline.generator
                   GENERATOR random_pipeline
                   AUTOSCHEDULER Halide::Mullapudi2016)
