cmake_minimum_required(VERSION 3.16)
project(interpret_nn)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

add_subdirectory(halide)

# The core library
add_library(interpret_nn STATIC interpret_nn.cpp ops.cpp)
target_link_libraries(interpret_nn PRIVATE ops_halide Halide::Runtime)


# tflite_parser needs the flatbuffers library, so let's install and build it
set(FLATBUFFERS_VER v1.12.0)
message(STATUS "Fetching flatbuffers ${FLATBUFFERS_VER}...")
include(FetchContent)
FetchContent_Declare(flatbuffers
                     GIT_REPOSITORY https://github.com/google/flatbuffers.git
                     GIT_TAG ${FLATBUFFERS_VER}
                     GIT_SHALLOW TRUE
                     GIT_SUBMODULES "")
FetchContent_MakeAvailable(flatbuffers)

# tflite_parser also need the tflite schema. We could keep a reference copy
# of the schema (in either .fbs or generated format), but instead we'll just
# pull a copy from the TF repo. FetchContent is overkill here.
set(TF_VER master)  # TODO: schema v3 vs v3a apparently not in a tag yet?
message(STATUS "Fetching TFLite schema ${TF_VER}...")
if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/tflite_schema_generated.h)
    file(DOWNLOAD https://github.com/tensorflow/tensorflow/raw/${TF_VER}/tensorflow/lite/schema/schema_generated.h
                  ${CMAKE_CURRENT_BINARY_DIR}/tflite_schema_generated.h)
endif()

add_library(tflite_schema INTERFACE)
target_include_directories(tflite_schema INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(tflite_schema INTERFACE flatbuffers)

# The tflite parser library
add_library(tflite_parser STATIC tflite_parser.cpp)
target_link_libraries(tflite_parser PRIVATE interpret_nn tflite_schema Halide::Runtime)

# Our benchmark
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE interpret_nn tflite_parser tflite_schema Halide::Runtime)

# Configure the benchmark as a 'test'
# add_test(NAME benchmark_interpret_nn COMMAND benchmark path/to/some/tflite)
# set_tests_properties(benchmark_interpret_nn PROPERTIES
#                      LABELS internal_app_tests
#                      PASS_REGULAR_EXPRESSION "Success!"
#                      SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
