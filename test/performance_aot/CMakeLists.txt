function(_define_peformance_aot NAME)
    set(options )
    set(oneValueArgs )
    set(multiValueArgs GENERATORS GEN_DEPS ENABLE_IF FEATURES)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    # add_halide_generator(${NAME}.generator
    #                      SOURCES ${NAME}.cpp)
    add_executable("${NAME}.generator" "${NAME}.cpp")
    target_compile_definitions("${NAME}.generator" PRIVATE HALIDE_COMPILING_GENERATOR)
    target_link_libraries("${NAME}.generator" PRIVATE Halide::Generator ${args_GEN_DEPS})

    set(TARGET "performance_aot_${NAME}")
    set(DEPS )
    foreach(G IN ITEMS ${args_GENERATORS})
        add_halide_library(${G}
                           FROM "${NAME}.generator"
                           GENERATOR "${G}"
                           FEATURES "${args_FEATURES}")
        list(APPEND DEPS ${G} ${G}.runtime)
    endforeach()

    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
        add_wasm_executable("${TARGET}"
                            SRCS "${NAME}.cpp"
                            DEPS "${DEPS}"
                            INCLUDES
                            "${Halide_BINARY_DIR}/include"
                            "${Halide_SOURCE_DIR}/test/common"
                            "${Halide_SOURCE_DIR}/tools"
                            "${CMAKE_CURRENT_BINARY_DIR}")

        add_wasm_halide_test("${TARGET}" GROUPS performance_aot)
    else ()
        add_executable("${TARGET}" "${NAME}.cpp")
        target_include_directories("${TARGET}"
                                   PRIVATE
                                   "${Halide_SOURCE_DIR}/test/common"
                                   "${Halide_SOURCE_DIR}/tools")
        target_link_libraries(${TARGET} PRIVATE ${DEPS})

        # TODO(#4938): remove need for these definitions
        if ("${Halide_TARGET}" MATCHES "opencl")
            target_compile_definitions("${TARGET}" PRIVATE TEST_OPENCL)
        endif ()
        if ("${Halide_TARGET}" MATCHES "metal")
            target_compile_definitions("${TARGET}" PRIVATE TEST_METAL)
        endif ()
        if ("${Halide_TARGET}" MATCHES "cuda")
            target_compile_definitions("${TARGET}" PRIVATE TEST_CUDA)
        endif ()
        add_halide_test("${TARGET}" GROUPS performance_aot)
    endif ()
endfunction()

_define_peformance_aot(async_gpu
                       GENERATORS with_async without_async)

