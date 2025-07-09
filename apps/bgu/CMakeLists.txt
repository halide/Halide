cmake_minimum_required(VERSION 3.28)
project(bgu)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(bgu.generator SOURCES bgu_generator.cpp)

# Filters
add_halide_library(bgu FROM bgu.generator)
add_halide_library(bgu_auto_schedule FROM bgu.generator
                   GENERATOR bgu
                   AUTOSCHEDULER Halide::Mullapudi2016
# Note(antonysigma): experimental GPU schedule failed on the Buildbot worker
# "halide-testbranch-main-llvm18-x86-64-linux-cmake" with error:
#
# CUDA error: CUDA_ERROR_ILLEGAL_ADDRESS cuCtxSynchronize failed
#
# Curiously, it works on a low-end GPU: Nvidia GTX 1660S.
#
# Uncomment the following code to debug. PARAMS
# autoscheduler.experimental_gpu_schedule=1
)

# Main executable
add_executable(bgu_filter filter.cpp)
target_link_libraries(bgu_filter
                      PRIVATE
                      Halide::ImageIO
                      bgu
                      bgu_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME bgu_filter COMMAND bgu_filter rgb.png out.png)
    set_tests_properties(bgu_filter PROPERTIES
                         LABELS bgu
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
