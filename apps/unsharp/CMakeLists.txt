cmake_minimum_required(VERSION 3.28)
project(unsharp)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(unsharp.generator SOURCES unsharp_generator.cpp)

set(_unsharp_autoscheduler_params autoscheduler.experimental_gpu_schedule=1)

if(Halide_TARGET MATCHES "cuda|opencl|vulkan")
    # Last level cache size estimate of the Nvidia GPU on the Buildbot. Hand
    # tuned to pass the Builbot tests.
    list(APPEND _unsharp_autoscheduler_params
        autoscheduler.last_level_cache_size=20000
    )
elseif(Halide_TARGET MATCHES "metal")
    # Resolving Metal error regarding the threads per GPU block limit:
    #
    # -[MTLDebugComputeCommandEncoder _validateThreadsPerThreadgroup:]:1267:
    # failed assertion `(threadsPerThreadgroup.width(70) *
    # threadsPerThreadgroup.height(8) * threadsPerThreadgroup.depth(1))(560)
    # must be <= 448. (kernel threadgroup size limit)`
    list(APPEND _unsharp_autoscheduler_params
        autoscheduler.last_level_cache_size=1000)
endif()

# Filters
add_halide_library(unsharp FROM unsharp.generator)
add_halide_library(unsharp_auto_schedule FROM unsharp.generator
                   GENERATOR unsharp
                   AUTOSCHEDULER Halide::Mullapudi2016
                   PARAMS ${_unsharp_autoscheduler_params})

# Main executable
add_executable(unsharp_filter filter.cpp)
target_link_libraries(unsharp_filter
                      PRIVATE
                      Halide::ImageIO
                      unsharp
                      unsharp_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgba.png COPYONLY)
    add_test(NAME unsharp_filter COMMAND unsharp_filter rgba.png out.png)
    set_tests_properties(unsharp_filter PROPERTIES
                         LABELS unsharp
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
