
add_library(common_halide STATIC common_halide.cpp)
target_link_libraries(common_halide PRIVATE Halide::Halide)
target_include_directories(common_halide PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

set(_HANNK_HALIDE_TARGETS "")

# Function to reduce boilerplate
function(_add_hannk_halide_library)
    set(options)
    set(oneValueArgs TARGET GENERATOR_NAME)
    set(multiValueArgs SRCS FEATURES GENERATOR_ARGS GENERATOR_DEPS FEATURES)
    cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${args_TARGET}.generator ${args_SRCS})
    target_link_libraries(${args_TARGET}.generator PRIVATE ${args_GENERATOR_DEPS} common_halide Halide::Generator)
    target_include_directories(${args_TARGET}.generator PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

    add_halide_library(${args_TARGET} FROM ${args_TARGET}.generator
                       NAMESPACE hannk
                       GENERATOR ${args_GENERATOR_NAME}
                       FEATURES large_buffers c_plus_plus_name_mangling ${args_FEATURES}
                       PARAMS ${args_GENERATOR_ARGS})

    set(_HANNK_HALIDE_TARGETS ${_HANNK_HALIDE_TARGETS} ${args_TARGET} PARENT_SCOPE)
endfunction()

_add_hannk_halide_library(
        TARGET add_uint8_uint8
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Add
        GENERATOR_ARGS
        GENERATOR_DEPS elementwise_program)

_add_hannk_halide_library(
        TARGET average_pool_uint8
        SRCS pool_generator.cpp
        GENERATOR_NAME AveragePool
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET conv_uint8
        SRCS conv_generator.cpp
        GENERATOR_NAME Conv
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET copy_uint8_uint8
        SRCS copy_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Copy
        GENERATOR_ARGS input.type=uint8 output.type=uint8
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET depthwise_conv_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME DepthwiseConv
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET depthwise_conv_broadcast_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME DepthwiseConv
        GENERATOR_ARGS inv_depth_multiplier=0
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET depthwise_conv_dm1_uint8
        SRCS depthwise_conv_generator.cpp
        GENERATOR_NAME DepthwiseConv
        GENERATOR_ARGS inv_depth_multiplier=1
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET fill_uint8
        SRCS fill_generator.cpp
        FEATURES no_bounds_query no_asserts
        GENERATOR_NAME Fill
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET fully_connected_uint8_uint8
        SRCS fully_connected_generator.cpp
        GENERATOR_NAME FullyConnected
        GENERATOR_ARGS output.type=uint8
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET fully_connected_uint8_int16
        SRCS fully_connected_generator.cpp
        GENERATOR_NAME FullyConnected
        GENERATOR_ARGS output.type=int16
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET elementwise_5xuint8_1xuint8
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Elementwise
        GENERATOR_ARGS inputs.size=5 inputs.type=uint8 output1_type=uint8
        GENERATOR_DEPS elementwise_program)

_add_hannk_halide_library(
        TARGET elementwise_5xint16_1xuint8int16
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Elementwise
        GENERATOR_ARGS inputs.size=5 inputs.type=int16 output1_type=uint8 output2_type=int16
        GENERATOR_DEPS elementwise_program)

_add_hannk_halide_library(
        TARGET l2_normalization_uint8
        SRCS normalizations_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME L2Normalization
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET max_pool_uint8
        SRCS pool_generator.cpp
        GENERATOR_NAME MaxPool
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET mean_uint8
        SRCS reductions_generator.cpp
        GENERATOR_NAME Mean
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET mul_uint8_uint8_uint8
        SRCS elementwise_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Mul
        GENERATOR_ARGS
        GENERATOR_DEPS elementwise_program)

_add_hannk_halide_library(
        TARGET softmax_uint8
        SRCS normalizations_generator.cpp
        FEATURES no_bounds_query
        GENERATOR_NAME Softmax
        GENERATOR_ARGS
        GENERATOR_DEPS)

_add_hannk_halide_library(
        TARGET tile_conv_filter_uint8
        SRCS conv_generator.cpp
        GENERATOR_NAME TileConvFilter
        GENERATOR_ARGS
        GENERATOR_DEPS)

add_library(op_impls INTERFACE)
target_link_libraries(op_impls INTERFACE ${_HANNK_HALIDE_TARGETS})
target_include_directories(op_impls INTERFACE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>)
