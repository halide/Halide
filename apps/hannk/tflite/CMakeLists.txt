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

# Enable this to see details about downloading -- useful for debugging
# set(FETCHCONTENT_QUIET NO)

# tflite includes neon2sse transitively, but it's pinned to an old version
# that declares a minimum CMake version of 3.0. We override it here to a
# new enough version that CMake 4+ won't refuse to build it.
FetchContent_Declare(
    neon2sse
    GIT_REPOSITORY https://github.com/intel/ARM_NEON_2_x86_SSE
    GIT_TAG 4732aac1e6c02984a12635d85c4644c2ffe585ca
)

FetchContent_Declare(
    tflite
    GIT_REPOSITORY https://github.com/tensorflow/tensorflow
    GIT_TAG ${TFLITE_TAG}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR tensorflow/lite/c
)

block(SCOPE_FOR POLICIES VARIABLES)
    # Suppress warnings about rotting CMake code we don't control
    set(CMAKE_POLICY_DEFAULT_CMP0135 OLD) # DOWNLOAD_EXTRACT_TIMESTAMP
    set(CMAKE_POLICY_DEFAULT_CMP0169 OLD) # FetchContent_Populate
    set(CMAKE_POLICY_DEFAULT_CMP0177 OLD) # install() path normalization

    # Configuration for tflite + upstream deps
    set(ABSL_PROPAGATE_CXX_STD ON)

    # Suppress warnings in tflite code
    add_compile_options(
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-anon-enum-enum-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-deprecated-this-capture>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow-uncaptured-local>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-tautological-type-limit-compare>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-template>
    )

    FetchContent_MakeAvailable(tflite)

    set_property(TARGET tensorflowlite_c PROPERTY EXCLUDE_FROM_ALL TRUE)
endblock()

FetchContent_GetProperties(tflite)

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
