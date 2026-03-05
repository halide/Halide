cmake_minimum_required(VERSION 3.28)
project(HelloiOS CXX C)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# This CMakeLists.txt only supports host builds for testing generators.
# For iOS app builds, use the checked-in Xcode project: HelloiOS.xcodeproj
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR
        "This CMakeLists.txt does not support iOS cross-compilation.\n"
        "For iOS app builds, use the Xcode project:\n"
        "  open HelloiOS.xcodeproj\n"
        "Then build and run in Xcode (Cmd+R).\n"
        "\n"
        "This CMakeLists.txt is only for testing generators on the host platform.")
endif()

message(STATUS "HelloiOS: Building host test only")

# Build the generator for host
add_halide_generator(reaction_diffusion_2.generator
    SOURCES HelloiOS/reaction_diffusion_2_generator.cpp)

# Generate libraries for host platform
add_halide_library(reaction_diffusion_2_init FROM reaction_diffusion_2.generator
    GENERATOR reaction_diffusion_2_init
    FUNCTION_NAME reaction_diffusion_2_init)

add_halide_library(reaction_diffusion_2_update FROM reaction_diffusion_2.generator
    GENERATOR reaction_diffusion_2_update
    FUNCTION_NAME reaction_diffusion_2_update)

add_halide_library(reaction_diffusion_2_render FROM reaction_diffusion_2.generator
    GENERATOR reaction_diffusion_2_render
    FUNCTION_NAME reaction_diffusion_2_render)

# Create a test executable that exercises the generators
add_executable(test_reaction_diffusion_2 test_reaction_diffusion_2.cpp)
target_link_libraries(test_reaction_diffusion_2 PRIVATE
    reaction_diffusion_2_init
    reaction_diffusion_2_update
    reaction_diffusion_2_render
    Halide::Runtime)

# Add CTest
add_test(NAME HelloiOS_generators_test
         COMMAND test_reaction_diffusion_2)
set_tests_properties(HelloiOS_generators_test PROPERTIES
                     LABELS HelloiOS)

message(STATUS "HelloiOS: Host test target configured")
message(STATUS "  Run test: ctest -V")
message(STATUS "  For iOS app, use the Xcode project: open HelloiOS.xcodeproj")
