cmake_minimum_required(VERSION 3.22)

project(HelloBaremetal-Super)

include(ExternalProject)

# The following variables are for 2 sub-projects to communicate.
# Alternatively, you may rely on the default naming rules defined in HalideGeneratorHelpers.cmake
set(GEN_PACKAGE "HelloBaremetal-add_generator")
set(GEN_NAMESPACE "HelloBaremetal::generators::")
set(GEN_EXE "add.generator")
set(GEN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generator)

# Step 1
# Build generator executable with host compiler in external child project
# and export as a package for CMake find_package()
ExternalProject_Add(gen_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generator
    BINARY_DIR ${GEN_BINARY_DIR}
    INSTALL_COMMAND ""
    CMAKE_ARGS
      -DGEN_PACKAGE=${GEN_PACKAGE}
      -DGEN_NAMESPACE=${GEN_NAMESPACE}
      -DGEN_EXE=${GEN_EXE}
)

# Step 2
# Build application with cross compiler,
# where the generator executable built in Step 1 is imported and called

ExternalProject_Add(app_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app
    INSTALL_COMMAND ""
    DEPENDS gen_project  # To make sure generator is ready
    CMAKE_ARGS
      -DCMAKE_TOOLCHAIN_FILE=${APP_TOOLCHAIN_FILE}
      -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/bin
      -D${GEN_PACKAGE}_ROOT:FILEPATH=${GEN_BINARY_DIR}
      -DGEN_TARGET=${GEN_NAMESPACE}${GEN_EXE}
)
