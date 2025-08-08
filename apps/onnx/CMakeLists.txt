cmake_minimum_required(VERSION 3.28)
project(onnx)

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
    GIT_TAG c11b6a715daeb48b056e4c9bed3dfc0dcb243d12  # v1.4.1
    SOURCE_SUBDIR do-not-load/
)
FetchContent_MakeAvailable(onnx)
set(ONNX_PROTO_FILE ${onnx_SOURCE_DIR}/onnx/onnx.proto)

# Generate sources from onnx.proto
add_custom_target(onnx_proto)
protobuf_generate(
    TARGET onnx_proto
    LANGUAGE cpp
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnx
    PROTOC_OUT_VAR onnx_proto_generated_sources
    PROTOS ${ONNX_PROTO_FILE}
    IMPORT_DIRS ${CMAKE_CURRENT_BINARY_DIR}
)

# Add library that converts ONNX models to Halide operators
add_library(oclib STATIC onnx_converter.cc ${CMAKE_CURRENT_BINARY_DIR}/onnx/onnx.pb.cc)
target_include_directories(
    oclib
    PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(oclib PUBLIC Halide::Halide ${Protobuf_LITE_LIBRARIES})
target_compile_definitions(oclib PUBLIC GOOGLE_PROTOBUF_NO_RTTI)

add_executable(onnx_converter_test onnx_converter_test.cc)
target_link_libraries(onnx_converter_test PRIVATE oclib)

add_test(NAME onnx_converter_test COMMAND onnx_converter_test)
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
    LINK_LIBRARIES oclib
)

# Generate test onnx model
add_custom_command(
    OUTPUT test_model.onnx
    COMMAND
    ${CMAKE_COMMAND} -E cat ${CMAKE_CURRENT_SOURCE_DIR}/test_model_proto.txt
    | protoc --encode=onnx.ModelProto --proto_path=${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/onnx.proto
    > test_model.onnx
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/onnx.proto
    COMMENT "Generating test ONNX model from proto content"
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/test_model_proto.txt
    VERBATIM
)
add_custom_target(test_model_onnx DEPENDS test_model.onnx)
add_dependencies(test_model_onnx onnx_proto)

# Generate static library using halide generator for test onnx model
add_halide_library(
    test_model FROM onnx_converter.generator
    GENERATOR onnx_model_generator
    PARAMS model_file_path=${CMAKE_CURRENT_BINARY_DIR}/test_model.onnx
    AUTOSCHEDULER Halide::Adams2019
)
add_dependencies(test_model test_model_onnx)

# Test the generated static library
add_executable(onnx_converter_generator_test onnx_converter_generator_test.cc)
target_link_libraries(onnx_converter_generator_test PRIVATE Halide::Runtime test_model)

add_test(NAME onnx_converter_generator_test COMMAND onnx_converter_generator_test)
set_tests_properties(
    onnx_converter_generator_test
    PROPERTIES
    LABELS onnx
    PASS_REGULAR_EXPRESSION "Success!"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
)

# Python bindings to convert onnx models to Halide model
find_package(Python3 COMPONENTS Interpreter Development)
if (NOT Python3_FOUND)
    message(WARNING "Could NOT find Python3")
    return()
endif ()

find_package(pybind11)
if (NOT pybind11_FOUND)
    message(WARNING "Could NOT find pybind11")
    return()
endif ()

pybind11_add_module(model_cpp model.cpp)
target_link_libraries(model_cpp PRIVATE Halide::Halide oclib)

add_test(
    NAME model_test
    COMMAND ${Python3_EXECUTABLE} -m unittest ${CMAKE_CURRENT_SOURCE_DIR}/model_test.py -v
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(
    model_test PROPERTIES
    LABELS onnx
    PASS_REGULAR_EXPRESSION "OK"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:model_cpp>;MODEL_AUTOSCHEDULER=$<TARGET_FILE:Halide::Adams2019>"
)

add_test(
    NAME halide_as_onnx_backend_test
    COMMAND ${Python3_EXECUTABLE} -m unittest ${CMAKE_CURRENT_SOURCE_DIR}/halide_as_onnx_backend_test.py -v
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(
    halide_as_onnx_backend_test PROPERTIES
    LABELS onnx
    PASS_REGULAR_EXPRESSION "OK"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:model_cpp>;MODEL_AUTOSCHEDULER=$<TARGET_FILE:Halide::Adams2019>"
    TIMEOUT 120
)
