cmake_minimum_required(VERSION 3.16)
project(hannk_halide)

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# ----------------------------

if (NOT CMAKE_CROSSCOMPILING)
    find_package(Halide REQUIRED)
    add_library(common_halide STATIC common_halide.cpp)
    target_link_libraries(common_halide PRIVATE Halide::Halide)
    target_include_directories(common_halide PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
endif()

# ----------------------------

function(_begin_halide_library_set LIBRARY_SET)
    set(${LIBRARY_SET}_CMAKE_LIB_LIST "" PARENT_SCOPE)
    if (CMAKE_CROSSCOMPILING)
        # Make a subdir to put all the forced-host-mode stuff.
        # Make it a double-subdir so that include paths of the form "halide/foo.h" can work as-is.
        set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_build_dir/halide")
        set(${LIBRARY_SET}_GEN_DIR "${GEN_DIR}" PARENT_SCOPE)

        file(MAKE_DIRECTORY ${GEN_DIR})

        # Unset CC, CXX, etc so that the sub-cmake file picks up host defaults, rather than (e.g.) emcc
        set(CC_SAVE "$ENV{CC}")
        set(CXX_SAVE "$ENV{CXX}")
        set(ENV{CC} "")
        set(ENV{CXX} "")
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                    "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
                    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                    "-DHalide_DIR=${Halide_DIR}"
                    "-DHalideHelpers_DIR=${HalideHelpers_DIR}"
                    "-Dhannk_SOURCE_DIR=${hannk_SOURCE_DIR}"
                    "${CMAKE_CURRENT_SOURCE_DIR}"
            WORKING_DIRECTORY ${GEN_DIR}
        )
        set(ENV{CC} "${CC_SAVE}")
        set(ENV{CXX} "${CXX_SAVE}")
    else()
        find_package(Halide REQUIRED)
        # TODO: when crosscompiling, the "runtime.a" files don't get produced;
        # create a fake target that we can add them to as depdendencies, then
        # pass the fake target to the sub-build.
        add_custom_target("${LIBRARY_SET}.build_all")
    endif()
endfunction()

function(_add_halide_library_set LIBRARY_SET)
    set(options)
    set(oneValueArgs TARGET GENERATOR_NAME)
    set(multiValueArgs SRCS GENERATOR_ARGS GENERATOR_DEPS FEATURES)
    cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (CMAKE_CROSSCOMPILING)
        set(GEN_DIR "${${LIBRARY_SET}_GEN_DIR}")
        foreach (t IN ITEMS "${args_TARGET}" "${args_TARGET}.runtime")
            add_library("${t}" STATIC IMPORTED GLOBAL)
            add_dependencies("${t}" ${LIBRARY_SET}.crosscompile)
            set_target_properties("${t}" PROPERTIES
                                  IMPORTED_LOCATION "${GEN_DIR}/${t}.a")
        endforeach()
    else()
        add_executable(${args_TARGET}.generator ${args_SRCS})
        target_link_libraries(${args_TARGET}.generator PRIVATE ${args_GENERATOR_DEPS} common_halide Halide::Generator)
        target_include_directories(${args_TARGET}.generator PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)

        add_halide_library(${args_TARGET} FROM ${args_TARGET}.generator
                           NAMESPACE hannk
                           GENERATOR ${args_GENERATOR_NAME}
                           FEATURES c_plus_plus_name_mangling ${args_FEATURES}
                           PARAMS ${args_GENERATOR_ARGS})

        add_dependencies("${LIBRARY_SET}.build_all" "${args_TARGET}" "${args_TARGET}.runtime")
    endif()
    set(${LIBRARY_SET}_CMAKE_LIB_LIST ${${LIBRARY_SET}_CMAKE_LIB_LIST} "${args_TARGET}" PARENT_SCOPE)
endfunction()

function(_finish_halide_library_set LIBRARY_SET)
    if (CMAKE_CROSSCOMPILING)
        set(GEN_DIR "${${LIBRARY_SET}_GEN_DIR}")

        set(BYPRODUCTS "")
        set(LIB_LIST "")
        foreach (t IN LISTS ${LIBRARY_SET}_CMAKE_LIB_LIST)
            set(BYPRODUCTS ${BYPRODUCTS} "${GEN_DIR}/${t}.a" "${GEN_DIR}/${t}.runtime.a" "${GEN_DIR}/${t}.h")
            set(LIB_LIST ${LIB_LIST} "${t}" "${t}.runtime")
        endforeach()

        # TODO: set --parallel here, is this the best way?
        cmake_host_system_information(RESULT NUMCORES QUERY NUMBER_OF_LOGICAL_CORES)
        add_custom_target(${LIBRARY_SET}.crosscompile
            COMMAND "${CMAKE_COMMAND}"
                    --build "${GEN_DIR}"
                    --parallel ${NUMCORES}
                    --config $<CONFIG>
                    --target "${LIBRARY_SET}.build_all"
            BYPRODUCTS "${BYPRODUCTS}"
            WORKING_DIRECTORY "${GEN_DIR}"
            USES_TERMINAL
        )

        set(INCLUDE_PATH "${GEN_DIR}/..")
    else ()
        set(INCLUDE_PATH $<BUILD_INTERFACE:${hannk_BINARY_DIR}>)
        set(LIB_LIST ${${LIBRARY_SET}_CMAKE_LIB_LIST})
    endif ()

    add_library(${LIBRARY_SET} INTERFACE)
    target_link_libraries(${LIBRARY_SET} INTERFACE ${LIB_LIST})
    target_include_directories(${LIBRARY_SET} INTERFACE ${INCLUDE_PATH})
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

