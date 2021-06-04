include(FetchContent)

set(TFLITE_TAG "v${TFLITE_VERSION}")

message(STATUS "Fetching TFLite ${TFLITE_TAG}...")

# static linking makes life with TFLite much easier
set(TFLITE_C_BUILD_SHARED_LIBS OFF)

# We don't care about comparing against these delegates (yet),
# and disabling it reduces compile time meaningfully
set(TFLITE_ENABLE_RUY OFF)
set(TFLITE_ENABLE_XNNPACK OFF)

# Also have to disable some stuff dragged in by TFLite
set(FLATBUFFERS_BUILD_TESTS OFF)
set(FLATBUFFERS_INSTALL OFF)
set(FLATBUFFERS_BUILD_FLATC OFF)

FetchContent_Declare(tflite
                     GIT_REPOSITORY https://github.com/tensorflow/tensorflow
                     GIT_TAG ${TFLITE_TAG}
                     GIT_SHALLOW TRUE)

FetchContent_GetProperties(tflite)
if (NOT tflite_POPULATED)
    FetchContent_Populate(tflite)
    # Some of the subprojects (e.g. Eigen) are very noisy and emit status messages all the time.
    # Temporary ignore status messages while adding this to silence it. Ugly but effective.
    set(OLD_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    add_subdirectory(${tflite_SOURCE_DIR}/tensorflow/lite/c ${tflite_BINARY_DIR})
    set(CMAKE_MESSAGE_LOG_LEVEL ${OLD_CMAKE_MESSAGE_LOG_LEVEL})
endif ()


# tensorflowlite_c is implicitly declared by this FetchContent

# Disable some noisy warnings in abseil
foreach (LIB IN ITEMS
            absl_base
            absl_graphcycles_internal
            absl_malloc_internal
            absl_synchronization
            absl_time
            absl_time_zone)
    target_compile_options(${LIB}
                           PRIVATE
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-tautological-type-limit-compare>
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-template>
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow>
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow-uncaptured-local>
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-anon-enum-enum-conversion>)
endforeach ()

# Disable some noisy warnings in tflite
target_compile_options(tensorflow-lite
                       PRIVATE
                       $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-gnu-inline-cpp-without-extern>
                       $<$<CXX_COMPILER_ID:GNU>:-Wno-ignored-attributes>)

# TFLite 2.5 specifies set(CMAKE_CXX_STANDARD 11), which can cause link errors in gcc-7.x;
# override CXX_STANDARD for it to ensure we compile everything the same way.
# set_property(TARGET tensorflowlite_c PROPERTY CXX_STANDARD 17)

# Make an interface library that is just to get the tflite headers,
# without any implied linkage
add_library(tensorflowlite_headers INTERFACE)
target_include_directories(tensorflowlite_headers INTERFACE
                           $<BUILD_INTERFACE:${tflite_SOURCE_DIR}>)

# ----------------
add_library(tflite_parser STATIC tflite_parser.cpp)
target_include_directories(tflite_parser
                           PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>
                           PRIVATE $<BUILD_INTERFACE:${hannk_BINARY_DIR}>
                           $<BUILD_INTERFACE:${tflite_SOURCE_DIR}>)
# Ensure that the includes from TFLite's captive flatbuffer library get precedence;
# in case the system may have a too-old version installed.
target_include_directories(tflite_parser BEFORE
                           PRIVATE $<BUILD_INTERFACE:${FlatBuffers_SOURCE_DIR}/include>)
target_link_libraries(tflite_parser PRIVATE Halide::Runtime)
