cmake_minimum_required(VERSION 3.28)
project(hannk)

# We need to set this for some of the subprojects pulled in by TFLite (eg flatbuffers)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

enable_testing()

# ----------------------------

option(HANNK_AOT_HOST_ONLY "Only build AOT host tools for cross-compiling" OFF)

option(HANNK_BUILD_TFLITE "Build TFLite+Delegate for HANNK" ON)
if (HANNK_BUILD_TFLITE AND (Halide_TARGET MATCHES "wasm"))
    message(FATAL_ERROR "HANNK_BUILD_TFLITE must be OFF when targeting wasm")
endif ()
message(STATUS "HANNK_BUILD_TFLITE is ${HANNK_BUILD_TFLITE}")

# -fPIC is necessary for .so builds (at least on Linux); not necessary for the non-delegate
# builds but easier to enable it for everything.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Set up the version of TFLite we expect
# (We need to do this even if HANNK_BUILD_TFLITE is off,
# so that the .tflite file parser can get the right schema)
set(TFLITE_VERSION_MAJOR "2" CACHE STRING "Major version of TFLite to assume")
set(TFLITE_VERSION_MINOR "21" CACHE STRING "Minor version of TFLite to assume")
set(TFLITE_VERSION_PATCH "0" CACHE STRING "Patch version of TFLite to assume")
set(TFLITE_VERSION "${TFLITE_VERSION_MAJOR}.${TFLITE_VERSION_MINOR}.${TFLITE_VERSION_PATCH}")

# ----------------------------

add_compile_definitions(TFLITE_VERSION_MAJOR=${TFLITE_VERSION_MAJOR})
add_compile_definitions(TFLITE_VERSION_MINOR=${TFLITE_VERSION_MINOR})
add_compile_definitions(TFLITE_VERSION_PATCH=${TFLITE_VERSION_PATCH})
add_compile_definitions(HANNK_BUILD_TFLITE=$<BOOL:${HANNK_BUILD_TFLITE}>)

# ----------------------------

# Find HalideHelpers -- this is just the Runtime headers and CMake functions, but no libraries
find_package(HalideHelpers REQUIRED)
if (HANNK_BUILD_TFLITE)
    find_package(tensorflow-lite CONFIG REQUIRED)
endif ()

# Canonical include root for hannk headers (e.g. "util/error_util.h").
add_library(hannk_headers INTERFACE)
target_include_directories(hannk_headers INTERFACE
                           $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

# ----------------------------

add_subdirectory(halide)
if (HANNK_AOT_HOST_ONLY)
    # Don't add anything else to the build... everything for AOT is in the halide subdirectory
    return()
endif ()

add_subdirectory(interpreter)
add_subdirectory(tflite)
add_subdirectory(util)
if (HANNK_BUILD_TFLITE)
    add_subdirectory(delegate)
endif ()

# ----------------------------

# Benchmarking executable
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE
                      tflite_parser
                      interpreter
                      error_util
                      file_util
                      hannk_log_stderr
                      Halide::Tools  # for halide_benchmark.h
                      Halide::Runtime)

add_executable(compare_vs_tflite compare_vs_tflite.cpp)
target_link_libraries(compare_vs_tflite PRIVATE
                      hannk_log_stderr
                      model_runner
                      Halide::Runtime)
target_include_directories(compare_vs_tflite
                           PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

# TODO: Surely there's a better way to set Emscripten flags.
if (Halide_TARGET MATCHES "wasm")
    foreach (t IN ITEMS benchmark compare_vs_tflite)
        # Note: "SHELL:" prevents de-duplication of the -s flag.
        target_link_options(
            ${t} PRIVATE
            "SHELL:-s ALLOW_MEMORY_GROWTH=1"
            "SHELL:-s ENVIRONMENT=node"
            "SHELL:-s NODERAWFS"
            "SHELL:$<$<CONFIG:Debug>:-s ASSERTIONS=1>"
        )
    endforeach ()
endif ()

if (Halide_TARGET MATCHES "wasm" AND NODE_JS_EXECUTABLE)
    execute_process(COMMAND "${NODE_JS_EXECUTABLE}" --version
                    OUTPUT_VARIABLE NODE_JS_VERSION_RAW
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "v" "" NODE_JS_VERSION ${NODE_JS_VERSION_RAW})

    if (NODE_JS_VERSION VERSION_LESS "16.13")
        message(FATAL_ERROR "Halide requires Node v16.13 or later, but found ${NODE_JS_VERSION_RAW} at ${NODE_JS_EXECUTABLE}. Please set NODE_JS_EXECUTABLE on the CMake command line.")
    endif ()
endif ()

# Tests

set(TEST_FILES
    inception_v2_224_quant/000.DEPTHWISE_CONV_2D.tflite
    inception_v4_299_quant/000.CONV_2D.tflite
    inception_v4_299_quant/001.CONV_2D.tflite
    inception_v4_299_quant/002.CONV_2D.tflite
    inception_v4_299_quant/003.MAX_POOL_2D.tflite
    inception_v4_299_quant/004.CONV_2D.tflite
    inception_v4_299_quant/006.CONV_2D.tflite
    inception_v4_299_quant/008.CONV_2D.tflite
    inception_v4_299_quant/009.CONV_2D.tflite
    inception_v4_299_quant/013.MAX_POOL_2D.tflite
    inception_v4_299_quant/015.CONCATENATION.tflite
    inception_v4_299_quant/016.AVERAGE_POOL_2D.tflite
    inception_v4_299_quant/024.CONCATENATION.tflite
    inception_v4_299_quant/044.CONV_2D.tflite
    inception_v4_299_quant/045.CONV_2D.tflite
    inception_v4_299_quant/049.CONV_2D.tflite
    inception_v4_299_quant/052.MAX_POOL_2D.tflite
    inception_v4_299_quant/054.CONV_2D.tflite
    inception_v4_299_quant/057.CONCATENATION.tflite
    inception_v4_299_quant/069.CONCATENATION.tflite
    inception_v4_299_quant/081.CONCATENATION.tflite
    inception_v4_299_quant/093.CONCATENATION.tflite
    inception_v4_299_quant/105.CONCATENATION.tflite
    inception_v4_299_quant/117.CONCATENATION.tflite
    inception_v4_299_quant/118.AVERAGE_POOL_2D.tflite
    inception_v4_299_quant/129.CONCATENATION.tflite
    inception_v4_299_quant/140.CONV_2D.tflite
    inception_v4_299_quant/141.CONCATENATION.tflite
    inception_v4_299_quant/142.MAX_POOL_2D.tflite
    inception_v4_299_quant/149.CONCATENATION.tflite
    inception_v4_299_quant/178.AVERAGE_POOL_2D.tflite
    inception_v4_299_quant/189.CONCATENATION.tflite
    inception_v4_299_quant/191.CONCATENATION.tflite
    inception_v4_299_quant/192.AVERAGE_POOL_2D.tflite
    inception_v4_299_quant/194.SOFTMAX.tflite
    misc/L2_NORMALIZATION.tflite
    misc/LOGISTIC.tflite
    misc/MUL.tflite
    misc/SPACE_TO_DEPTH.2.tflite
    misc/SPACE_TO_DEPTH.4.tflite
    misc/SPLIT.1_1_1.tflite
    misc/SPLIT_V.36_5_7.tflite
    misc/TRANSPOSE.2_0_1.tflite
    misc/bad_broadcast_add.tflite
    misc/bad_broadcast_mul.tflite
    misc/bad_broadcast_sub.tflite
    misc/bad_fully_connected.tflite
    mobilenet_v1_0.25_128_quant/000.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/001.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/002.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/003.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/004.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/005.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/006.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/007.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/008.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/009.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/010.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/011.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/012.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/013.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/014.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/019.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/022.CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/025.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_0.25_128_quant/027.AVERAGE_POOL_2D.tflite
    mobilenet_v1_0.25_128_quant/029.RESHAPE.tflite
    mobilenet_v1_0.25_128_quant/030.SOFTMAX.tflite
    mobilenet_v1_1.0_224_quant/000.CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/001.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/002.CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/003.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/004.CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/005.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/006.CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/007.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/008.CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/009.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/013.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/025.DEPTHWISE_CONV_2D.tflite
    mobilenet_v1_1.0_224_quant/027.AVERAGE_POOL_2D.tflite
    mobilenet_v1_1.0_224_quant/029.RESHAPE.tflite
    mobilenet_v1_1.0_224_quant/030.SOFTMAX.tflite
    mobilenet_v2_1.0_224_quant/000.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/001.DEPTHWISE_CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/002.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/003.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/004.DEPTHWISE_CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/005.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/008.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/009.ADD.tflite
    mobilenet_v2_1.0_224_quant/012.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/015.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/016.ADD.tflite
    mobilenet_v2_1.0_224_quant/020.ADD.tflite
    mobilenet_v2_1.0_224_quant/021.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/022.DEPTHWISE_CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/023.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/027.ADD.tflite
    mobilenet_v2_1.0_224_quant/031.ADD.tflite
    mobilenet_v2_1.0_224_quant/032.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/033.DEPTHWISE_CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/034.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/035.ADD.tflite
    mobilenet_v2_1.0_224_quant/038.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/040.DEPTHWISE_CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/042.ADD.tflite
    mobilenet_v2_1.0_224_quant/045.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/046.ADD.tflite
    mobilenet_v2_1.0_224_quant/047.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/049.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/053.ADD.tflite
    mobilenet_v2_1.0_224_quant/054.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/056.CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/057.ADD.tflite
    mobilenet_v2_1.0_224_quant/059.DEPTHWISE_CONV_2D.tflite
    mobilenet_v2_1.0_224_quant/062.AVERAGE_POOL_2D.tflite
    mobilenet_v2_1.0_224_quant/064.RESHAPE.tflite
)
list(TRANSFORM TEST_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/test/")

foreach (t IN LISTS TEST_FILES)
    file(RELATIVE_PATH test_name ${hannk_SOURCE_DIR} ${t})

    # Emscripten sets CMAKE_CROSSCOMPILING_TOOLCHAIN to NODE_JS_EXECUTABLE,
    # which ensures these tests will run in Node.
    add_test(NAME ${test_name}
             COMMAND compare_vs_tflite ${t} --benchmark 0)

    set_tests_properties(${test_name} PROPERTIES
                         LABELS hannk_tests)
endforeach ()
