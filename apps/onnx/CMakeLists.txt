cmake_minimum_required(VERSION 3.28)
project(onnx_app)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Protobuf)
if (NOT Protobuf_FOUND)
    message(WARNING "Could NOT find Protobuf")
    return()
endif ()

find_package(Halide REQUIRED)

# Download onnx.proto
include(FetchContent)
FetchContent_Declare(
    onnx
    GIT_REPOSITORY https://github.com/onnx/onnx.git
    GIT_TAG e709452ef2bbc1d113faf678c24e6d3467696e83  # v1.18.0
    SOURCE_SUBDIR do-not-load/
)
FetchContent_MakeAvailable(onnx)

# Add library that converts ONNX models to Halide operators
add_library(oclib STATIC)
add_library(onnx_app::oclib ALIAS oclib)

target_sources(oclib PRIVATE onnx_converter.cc "${onnx_SOURCE_DIR}/onnx/onnx.proto")
protobuf_generate(
    TARGET oclib
    LANGUAGE cpp
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/_protoc_out
    IMPORT_DIRS ${onnx_SOURCE_DIR}
)
target_include_directories(oclib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/_protoc_out")
target_link_libraries(oclib PUBLIC Halide::Halide protobuf::libprotobuf-lite)
target_compile_definitions(oclib PUBLIC GOOGLE_PROTOBUF_NO_RTTI)

# Add test for the onnx converter library (oclib)
add_executable(onnx_converter_test onnx_converter_test.cc)
target_link_libraries(onnx_converter_test PRIVATE onnx_app::oclib)

add_test(NAME onnx_converter_test
         COMMAND onnx_converter_test)
set_tests_properties(
    onnx_converter_test PROPERTIES
    LABELS onnx
    PASS_REGULAR_EXPRESSION "Success!"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
)

# Generator
add_halide_generator(
    onnx_converter.generator
    SOURCES onnx_converter_generator.cc
    LINK_LIBRARIES onnx_app::oclib
)

# Generate test onnx model
add_custom_command(
    OUTPUT test_model.onnx
    COMMAND
    protobuf::protoc
    --encode=onnx.ModelProto
    "--proto_path=${onnx_SOURCE_DIR}"
    "$<SHELL_PATH:${onnx_SOURCE_DIR}/onnx/onnx.proto>"
    < "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/test_model_proto.txt>"
    > test_model.onnx
    DEPENDS protobuf::protoc ${onnx_SOURCE_DIR}/onnx/onnx.proto
    COMMENT "Generating test ONNX model from proto content"
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/test_model_proto.txt
    VERBATIM
)

# Generate static library using halide generator for test onnx model
add_halide_library(
    test_model FROM onnx_converter.generator
    GENERATOR onnx_model_generator
    PARAMS model_file_path=${CMAKE_CURRENT_BINARY_DIR}/test_model.onnx
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_model.onnx
    AUTOSCHEDULER Halide::Adams2019
)

# Test the generated static library
add_executable(onnx_converter_generator_test onnx_converter_generator_test.cc)
target_link_libraries(onnx_converter_generator_test PRIVATE Halide::Runtime test_model)

add_test(NAME onnx_converter_generator_test
         COMMAND onnx_converter_generator_test)
set_tests_properties(
    onnx_converter_generator_test
    PROPERTIES
    LABELS onnx
    PASS_REGULAR_EXPRESSION "Success!"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
)

# Python bindings to convert onnx models to Halide model
find_package(Python 3 COMPONENTS Interpreter Development)
if (NOT Python_FOUND)
    message(WARNING "Could NOT find Python")
    return()
endif ()

find_package(pybind11 HINTS "${Python_SITEARCH}")
if (NOT pybind11_FOUND)
    message(WARNING "Could NOT find pybind11")
    return()
endif ()

pybind11_add_module(
    model_cpp model.cpp benchmarking_utils.h common_types.h denormal_disabler.h
)
target_link_libraries(model_cpp PRIVATE Halide::Halide onnx_app::oclib)

add_test(
    NAME model_test
    COMMAND ${Python_EXECUTABLE} -m unittest ${CMAKE_CURRENT_SOURCE_DIR}/model_test.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME halide_as_onnx_backend_test
    COMMAND ${Python_EXECUTABLE} -m unittest ${CMAKE_CURRENT_SOURCE_DIR}/halide_as_onnx_backend_test.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(
    halide_as_onnx_backend_test
    model_test
    PROPERTIES
    LABELS onnx
    PASS_REGULAR_EXPRESSION "OK"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:model_cpp>;MODEL_AUTOSCHEDULER=$<TARGET_FILE:Halide::Adams2019>"
    TIMEOUT 120
)
