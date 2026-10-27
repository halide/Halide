cmake_minimum_required(VERSION 3.28)
project(python_abi3_consumer LANGUAGES C CXX)

set(CMAKE_DISABLE_FIND_PACKAGE_nanobind TRUE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Halide REQUIRED COMPONENTS Python)

set(bindings "${CMAKE_CURRENT_LIST_DIR}/../../../python_bindings")

# Wheels do not ship the optional helper archive; also exercise building it
# separately from the compiler extension, without nanobind.
if (NOT TARGET Halide::PyStubs)
    add_subdirectory("${bindings}/halide/stub" stub-helper)
endif ()

add_halide_generator(
    abi3.generator
    SOURCES "${bindings}/halide/test/generators/simplecpp_generator.cpp"
    PYSTUB simplecpp
)
add_halide_library(simplecpp FROM abi3.generator GENERATOR simplecpp PYTHON_EXTENSION python_source)
add_halide_python_extension_library(abi3_pipeline HALIDE_LIBRARIES simplecpp)

add_halide_generator(
    runtimeadd.generator
    SOURCES "${bindings}/halide-runtime/test/runtimeadd_generator.cpp"
)
add_halide_library(runtimeadd FROM runtimeadd.generator)
add_library(runtimeadd_module MODULE "${bindings}/halide-runtime/test/aot_module_stub.c")
target_link_libraries(runtimeadd_module PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,runtimeadd>")
target_link_options(runtimeadd_module PRIVATE "$<$<C_COMPILER_ID:MSVC>:/INCREMENTAL:NO>")
set_target_properties(runtimeadd_module PROPERTIES PREFIX "" OUTPUT_NAME runtimeadd)

install(
    TARGETS abi3.generator_pystub abi3_pipeline runtimeadd_module
    LIBRARY DESTINATION .
    RUNTIME DESTINATION .
)
