cmake_minimum_required(VERSION 3.16)
project(external_vk_demo)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Define Halide installation path
# I've compiled it with the changes like the following:
# cmake -G Ninja -S . -B build \               
#  -DCMAKE_BUILD_TYPE=Debug \
#  -DCMAKE_OSX_ARCHITECTURES=arm64 \
#  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
#  -DLLVM_DIR="$(brew --prefix llvm@19)/lib/cmake/llvm" \
#  -DHalide_WASM_BACKEND=wabt \
#  -DWITH_PYTHON_BINDINGS=OFF \
#  -DWITH_TUTORIALS=OFF \
#  -DBUILD_SHARED_LIBS=OFF \
#  -DHalide_BUNDLE_STATIC=ON
# Then build:
# cmake --build build -j32 
# Then install:
# cmake --install build --prefix "$PWD/install"
set(HALIDE_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../install)

# Create our own Halide targets exactly like the Bazel BUILD file

# Halide runtime headers (equivalent to ":runtime")
add_library(Halide_Runtime INTERFACE)
target_include_directories(Halide_Runtime INTERFACE ${HALIDE_INSTALL_DIR}/include)

# Halide static library (equivalent to ":lib_halide_static")
add_library(Halide_Static STATIC IMPORTED)
# FIX paths they are hardcoded to my machine
set_target_properties(Halide_Static PROPERTIES
    IMPORTED_LOCATION ${HALIDE_INSTALL_DIR}/lib/libHalide.a
    INTERFACE_INCLUDE_DIRECTORIES ${HALIDE_INSTALL_DIR}/include
    INTERFACE_LINK_LIBRARIES "-L/opt/homebrew/opt/llvm@19/lib;-lLLVM-19;-L/opt/homebrew/Cellar/wabt/1.0.37/lib;-lwabt;-lwasm-rt-impl;-L/opt/homebrew/Cellar/lld@19/19.1.7/lib;-llldWasm;-llldCommon;-framework Foundation;-framework CoreFoundation;-framework CoreGraphics;-framework CoreVideo;-framework IOKit;-framework AppKit;-lz;-ldl;-lpthread;-lcurses;-lffi"
)
target_link_libraries(Halide_Static INTERFACE Halide_Runtime)

# Halide GenGen library (equivalent to ":gengen")
add_library(Halide_GenGen STATIC IMPORTED)
set_target_properties(Halide_GenGen PROPERTIES
    IMPORTED_LOCATION ${HALIDE_INSTALL_DIR}/lib/libHalide_GenGen.a
    INTERFACE_INCLUDE_DIRECTORIES "${HALIDE_INSTALL_DIR}/include;${HALIDE_INSTALL_DIR}/share/Halide/tools"
    # Force load the GenGen library (equivalent to alwayslink = True)
    INTERFACE_LINK_LIBRARIES "-Wl,-force_load,${HALIDE_INSTALL_DIR}/lib/libHalide_GenGen.a"
)
target_link_libraries(Halide_GenGen INTERFACE Halide_Static)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Generator executable - create it manually like Bazel does
add_executable(convert_generator convert_generator.cc)
target_link_libraries(convert_generator 
                      PRIVATE 
                      Halide_GenGen
                      Vulkan::Vulkan)

# Manual AOT compilation using custom command (following Makefile pattern)
# Note: Requires Vulkan library to be in your path. Set environment before building:
# On macOS: export DYLD_LIBRARY_PATH=/path/to/vulkan/lib:$DYLD_LIBRARY_PATH
# On Linux: export LD_LIBRARY_PATH=/path/to/vulkan/lib:$LD_LIBRARY_PATH
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/convert_generator.a ${CMAKE_CURRENT_BINARY_DIR}/convert_generator.h
    COMMAND $<TARGET_FILE:convert_generator> -g convert_generator -o ${CMAKE_CURRENT_BINARY_DIR} target=host-vulkan-vk_int8
    DEPENDS convert_generator
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating Vulkan AOT library (ensure Vulkan is in library path)"
)

# Create custom target and library from generated files  
add_custom_target(convert_aot_files DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/convert_generator.a ${CMAKE_CURRENT_BINARY_DIR}/convert_generator.h)
add_library(convert_aot STATIC IMPORTED)
set_target_properties(convert_aot PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/convert_generator.a)
add_dependencies(convert_aot convert_aot_files)
target_link_libraries(convert_aot INTERFACE Vulkan::Vulkan Halide_Static)

# External Halide override library
add_library(external_halide_override 
            external_halide_override.cpp
            external_halide_override.h)
target_link_libraries(external_halide_override
                      PRIVATE
                      convert_aot
                      Halide_Static
                      Halide_GenGen
                      Vulkan::Vulkan)

# Vulkan app library
add_library(vulkan_app 
            vulkan_app.cpp
            vulkan_app.h)
target_include_directories(vulkan_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${HALIDE_INSTALL_DIR}/share/tools)
add_dependencies(vulkan_app convert_aot_files)  # Ensure generated header exists
target_link_libraries(vulkan_app
                      PRIVATE
                      Halide_Static
                      Vulkan::Vulkan
                      external_halide_override)

# Simple test executable to verify image loading
add_executable(demo_main demo_main.cpp)
target_include_directories(demo_main PRIVATE ${HALIDE_INSTALL_DIR}/share/tools)
target_link_libraries(demo_main
                      PRIVATE
                      vulkan_app
                      external_halide_override
                      convert_aot
                      Halide_Static
                      Halide_GenGen
                      Vulkan::Vulkan)

# Did not try the following, only used MacOS, on Windows we will prob fail as we override WEAK
if (NOT WIN32)
    target_link_libraries(external_halide_override PRIVATE dl pthread)
    target_link_libraries(vulkan_app PRIVATE dl pthread)
    target_link_libraries(demo_main PRIVATE dl pthread)
endif ()