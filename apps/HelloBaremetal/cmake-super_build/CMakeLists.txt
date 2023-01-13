cmake_minimum_required(VERSION 3.22)

project(HelloBaremetal-Super)

include(ExternalProject)

option(BAREMETAL "Compile for baremetal target" OFF)
set(GEN_PACKAGE "HelloBaremetal-add_generator")
set(GEN_PACKAGE_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/generator_install")
set(GEN_EXE "add.generator")

# Step 1
# Build generator executable with host compiler in external child project
# and export as a package for CMake find_package()
ExternalProject_Add(gen_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generator
    INSTALL_COMMAND ""
    CMAKE_ARGS
      -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
      -DGEN_PACKAGE=${GEN_PACKAGE}
      -DGEN_EXE=${GEN_EXE}
      -DINSTALL_DESTINATION=${GEN_PACKAGE_INSTALL}
)

# Step 2
# Build application with cross compiler,
# where the generator executable built in Step 1 is imported and called
if (BAREMETAL)
    set(TOOLCHAIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${APP_TOOLCHAIN_FILE})
endif()

ExternalProject_Add(app_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app
    INSTALL_COMMAND ""
    CMAKE_ARGS
      -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_PATH}
      -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/bin
      -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
      -DBAREMETAL=${BAREMETAL}
      -D${GEN_PACKAGE}_ROOT:FILEPATH=${GEN_PACKAGE_INSTALL}
      -DGEN_EXE=${GEN_EXE}
)
