cmake_minimum_required(VERSION 3.22)

# Enable assembly language (.s) support additionally
project(HelloBaremetal LANGUAGES C CXX ASM)

# Set up C++ language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

set(GEN_EXE "add.generator")
set(GEN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generator)

# Step 1
# Build generator executable with host compiler in external child project
# and export as a package for CMake find_package()
include(ExternalProject)
ExternalProject_Add(gen_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generator
    BINARY_DIR ${GEN_BINARY_DIR}
    INSTALL_COMMAND ""
    CMAKE_ARGS
      -DGEN_EXE=${GEN_EXE}
)


# Step 2
# Build application with cross compiler,
# where the generator executable built in Step 1 is imported and called

# Import Halide. Instead of the package Halide, HalideHelpers is enough as we don't use JIT mode.
find_package(HalideHelpers REQUIRED)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# NOTE : find_package(HelloBaremetal-add_generator REQUIRED) is not ready at configuration time.
# Instead, declare the generator executable as imported, which is built in external project
add_executable(${GEN_EXE} IMPORTED)
# Specify the location of exe file to import
set_property(TARGET ${GEN_EXE}
             PROPERTY IMPORTED_LOCATION ${GEN_BINARY_DIR}/${GEN_EXE})
# Associate with external project so that it invokes build porcess
add_dependencies(${GEN_EXE} gen_project)

# Generate filter function
add_halide_library(add
                   FROM ${GEN_EXE}
                   GENERATOR add
                   # You may set Target feature in addition to what is set in the toolchain file
                   FEATURES debug

                   # In baremetal, set parallelism=1 because thread support is unavailable.
                   AUTOSCHEDULER Halide::Mullapudi2016
                   PARAMS autoscheduler.parallelism=1)

# Main executable
add_executable(add_filter ${SRC_DIR}/filter.cpp)
target_link_libraries(add_filter PRIVATE
                      Halide::ImageIO
                      add)

# Depending on the target CPU, we need to enable Arm NEON manually
# if the executable contains any SIMD instructions.
target_sources(add_filter PRIVATE "$<$<BOOL:${BAREMETAL}>:${SRC_DIR}/enable_neon.s>")
# It is safe to do it in boot up process before main function (or C runtime) starts.
target_link_options(add_filter PRIVATE "$<$<BOOL:${BAREMETAL}>:--entry=_enable_neon>")
