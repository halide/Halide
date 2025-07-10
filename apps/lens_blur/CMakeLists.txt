cmake_minimum_required(VERSION 3.28)
project(lens_blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(lens_blur.generator SOURCES lens_blur_generator.cpp)

# Filters
add_halide_library(lens_blur FROM lens_blur.generator)
add_halide_library(lens_blur_auto_schedule FROM lens_blur.generator
                   GENERATOR lens_blur
                   AUTOSCHEDULER Halide::Mullapudi2016
                   PARAMS autoscheduler.last_level_cache_size=1000 autoscheduler.parallelism=4096 autoscheduler.experimental_gpu_schedule=1)

# Main executable
add_executable(lens_blur_filter process.cpp)
target_link_libraries(lens_blur_filter
                      PRIVATE
                      Halide::ImageIO
                      lens_blur
                      lens_blur_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb_small.png)
if (EXISTS ${IMAGE})
    if (Halide_TARGET MATCHES "metal")
        # Note(antonysigma): Buildbot error message:
        #
        # 2025-06-30 23:26:02.260 lens_blur_filter[32272:21031150] Metal API Validation
        # Enabled -[MTLDebugComputeCommandEncoder _validateThreadsPerThreadgroup:]:1267:
        # failed assertion `(threadsPerThreadgroup.width(32) *
        # threadsPerThreadgroup.height(32) * threadsPerThreadgroup.depth(1))(1024) must
        # be <= 896. (kernel threadgroup size limit)'
        #
        # Possible root cause: Autoscheduler's GPUTilingDedup::max_n_threads is
        # hardcoded to 1024 threads per block. The OSX Metal API caps the value at 836
        # threads per block because of the register pressure in lens_blur's GPU kernel.
        message ("Pipeline lens_blur_auto_schedule skipped for target host-metal")
    else ()
        configure_file(${IMAGE} rgb_small.png COPYONLY)
        add_test(NAME lens_blur_filter
                COMMAND lens_blur_filter rgb_small.png 32 13 0.5 32 3 out.png)
        set_tests_properties(lens_blur_filter PROPERTIES
                            LABELS lens_blur
                            PASS_REGULAR_EXPRESSION "Success!"
                            SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
    endif ()
endif ()
