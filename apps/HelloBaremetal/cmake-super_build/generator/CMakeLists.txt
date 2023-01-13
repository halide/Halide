cmake_minimum_required(VERSION 3.22)

project(HelloBaremetal-gen)

if (NOT DEFINED GEN_EXE OR NOT DEFINED GEN_PACKAGE OR NOT DEFINED INSTALL_DESTINATION)
    message(FATAL_ERROR "CMakeVariable GEN_EXE or GEN_PACKAGE or INSTALL_DESTINATION is not set")
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

# Export the built generator executable as package
file(MAKE_DIRECTORY "${INSTALL_DESTINATION}/cmake")
set(EXPORT_FILE "${INSTALL_DESTINATION}/cmake/${GEN_PACKAGE}Config.cmake")
export(TARGETS ${GEN_EXE} APPEND FILE "${EXPORT_FILE}")
