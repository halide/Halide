add_custom_target(hannk-halide_generators)

function(_export_host_target TARGET)
    add_dependencies(hannk-halide_generators ${TARGET})
    export(TARGETS ${TARGET}
           NAMESPACE hannk::halide_generators::
           APPEND FILE "${hannk_BINARY_DIR}/hannk-halide_generators-config.cmake")
endfunction()

# Emscripten tries to disable finding packages outside its sysroot,
# but we need our native generators. Setting CMAKE_FIND_ROOT_PATH_BOTH
# here overrides Emscripten's search preference.
find_package(hannk-halide_generators QUIET CMAKE_FIND_ROOT_PATH_BOTH)

if (hannk-halide_generators_FOUND)
    message(STATUS "Using hannk-halide_generators from ${hannk-halide_generators_DIR}")
else ()
    if (CMAKE_CROSSCOMPILING)
        message(WARNING
                "hannk-halide_generators were not found and it looks like you are cross-compiling. "
                "This is likely to fail. Please set -Dhannk-halide_generators_ROOT=... at the CMake "
                "command line to the build directory of a host-built hannk.")
    endif ()
    find_package(Halide REQUIRED)
endif ()

if (NOT TARGET hannk::halide_generators::common_halide)
    add_library(common_halide STATIC common_halide.cpp)
    add_library(hannk::halide_generators::common_halide ALIAS common_halide)

    target_link_libraries(common_halide PRIVATE Halide::Halide)
    target_include_directories(common_halide PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

    _export_host_target(common_halide)
endif ()

# ----------------------------

function(_begin_halide_library_set LIBRARY_SET)
    add_library(${LIBRARY_SET} INTERFACE)
    target_include_directories(${LIBRARY_SET} INTERFACE "$<BUILD_INTERFACE:${hannk_BINARY_DIR}>")

    add_custom_target(${LIBRARY_SET}.build_all)
endfunction()

function(_add_halide_library_set LIBRARY_SET)
    set(options)
    set(oneValueArgs TARGET GENERATOR_NAME)
    set(multiValueArgs SRCS GENERATOR_ARGS GENERATOR_DEPS FEATURES)
    cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(gen hannk::halide_generators::${args_TARGET}.generator)

    if (NOT TARGET ${gen})
        add_executable(${args_TARGET}.generator ${args_SRCS})
        add_executable(${gen} ALIAS ${args_TARGET}.generator)

        target_link_libraries(${args_TARGET}.generator PRIVATE ${args_GENERATOR_DEPS} common_halide Halide::Generator)
        target_include_directories(${args_TARGET}.generator PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

        _export_host_target(${args_TARGET}.generator)
    endif ()

    add_halide_library(${args_TARGET} FROM ${gen}
                       NAMESPACE hannk
                       GENERATOR ${args_GENERATOR_NAME}
                       FEATURES c_plus_plus_name_mangling ${args_FEATURES}
                       PARAMS ${args_GENERATOR_ARGS})

    add_dependencies("${LIBRARY_SET}.build_all" "${args_TARGET}" "${args_TARGET}.runtime")
    target_link_libraries(${LIBRARY_SET} INTERFACE ${args_TARGET})
endfunction()

function(_finish_halide_library_set LIBRARY_SET)
    # Nothing to do for now.
endfunction()

# ---------------------------

_begin_halide_library_set(halide_op_implementations)

_add_halide_library_set(halide_op_implementations
        TARGET add_uint8_uint8
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Add
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET average_pool_uint8
        SRCS pool_generator.cpp
        GENERATOR_NAME AveragePool
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET conv_u8_u8_u8
        SRCS conv_generator.cpp
        GENERATOR_NAME Conv
        GENERATOR_ARGS output.type=uint8
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET conv_u8_u8_i16
        SRCS conv_generator.cpp
        GENERATOR_NAME Conv
        GENERATOR_ARGS output.type=int16
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET copy_uint8_uint8
        SRCS copy_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Copy
        GENERATOR_ARGS input.type=uint8 output.type=uint8
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET depthwise_conv_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME DepthwiseConv
        GENERATOR_ARGS inv_depth_multiplier=1
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET depthwise_conv_broadcast_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME DepthwiseConv
        GENERATOR_ARGS inv_depth_multiplier=0
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET depthwise_conv_shallow_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME DepthwiseConv
        GENERATOR_ARGS inv_depth_multiplier=1 shallow=true
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET fill_uint8
        SRCS fill_generator.cpp
        FEATURES no_bounds_query no_asserts
        GENERATOR_NAME Fill
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET elementwise_5xuint8_1xuint8
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Elementwise
        GENERATOR_ARGS inputs.size=5 inputs.type=uint8 output1_type=uint8
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET elementwise_5xint16_1xuint8int16
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Elementwise
        GENERATOR_ARGS inputs.size=5 inputs.type=int16 output1_type=uint8 output2_type=int16
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET l2_normalization_uint8
        SRCS normalizations_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME L2Normalization
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET max_pool_uint8
        SRCS pool_generator.cpp
        GENERATOR_NAME MaxPool
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET mean_uint8
        SRCS reductions_generator.cpp
        GENERATOR_NAME Mean
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET mul_uint8_uint8_uint8
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Mul
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET softmax_uint8
        SRCS normalizations_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Softmax
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET tile_conv_filter_uint8
        SRCS conv_generator.cpp
        GENERATOR_NAME TileConvFilter
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_halide_library_set(halide_op_implementations
        TARGET upsample_channels_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME UpsampleChannels
        GENERATOR_ARGS
        GENERATOR_DEPS)

_finish_halide_library_set(halide_op_implementations)

