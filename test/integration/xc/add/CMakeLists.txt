cmake_minimum_required(VERSION 3.16)
project(xc-add)

enable_testing()

##
## Dependencies
##

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Need to load generators from stage when cross compiling
    find_package(xc-generators REQUIRED)
endif ()

find_package(HalideHelpers REQUIRED)

##
## Generate library
##

add_halide_library(add FROM xc::add_gen
                   TARGETS cmake
                   REGISTRATION add_reg_cpp)
add_library(xc::add ALIAS add)

##
## Helper runner
##

add_executable(run_add ${add_reg_cpp})
target_link_libraries(run_add PRIVATE add Halide::RunGenMain)

##
## Test helper
##

add_test(NAME run_add
         COMMAND run_add --output_extents=[10,10] --benchmarks=all)

##
## Installation
##

include(GNUInstallDirs)

set(xc-add_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/cmake/xc-add"
    CACHE PATH "Path to CMake files")

install(TARGETS add add.runtime
        EXPORT xc-add-config)

install(EXPORT xc-add-config
        DESTINATION "${xc-add_INSTALL_CMAKEDIR}"
        NAMESPACE xc::)
