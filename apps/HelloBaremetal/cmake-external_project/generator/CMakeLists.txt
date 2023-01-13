cmake_minimum_required(VERSION 3.22)

project(HelloBaremetal-gen)

if (NOT DEFINED GEN_EXE OR NOT DEFINED INSTALL_DESTINATION)
    message(FATAL_ERROR "CMakeVariable GEN_EXE or INSTALL_DESTINATION is not set")
endif()

# Set up C++ language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Halide REQUIRED)  # We need Halide::Generator

# Build generator
add_executable(${GEN_EXE} ${SRC_DIR}/add_generator.cpp)
target_link_libraries(${GEN_EXE} PRIVATE Halide::Generator)

# cmake's install() command removes runtime path information and that causes "cannot find libHalide.so".
# To cope with that, the below statement preserves RPATH information.
set_target_properties(${GEN_EXE} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

# Locate the built generator executable
install(TARGETS ${GEN_EXE} RUNTIME DESTINATION ${INSTALL_DESTINATION})
