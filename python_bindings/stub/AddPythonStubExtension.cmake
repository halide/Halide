set(HALIDE_PYSTUB_CPP_PATH ${CMAKE_CURRENT_LIST_DIR}/PyStub.cpp)

function(add_python_stub_extension TARGET)
    set(options)
    set(oneValueArgs GENERATOR MODULE)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_GENERATOR)
        set(ARG_GENERATOR "${TARGET}")
    endif ()

    if (NOT ARG_MODULE)
        set(ARG_MODULE "${TARGET}_stub")
    endif ()

    Python3_add_library(${TARGET} MODULE ${HALIDE_PYSTUB_CPP_PATH})
    target_compile_definitions(${TARGET} PRIVATE
                               "HALIDE_PYSTUB_GENERATOR_NAME=${ARG_GENERATOR}"
                               "HALIDE_PYSTUB_MODULE_NAME=${ARG_MODULE}")
    target_link_libraries(${TARGET} PRIVATE Halide::PyStubs Halide::Halide)
    set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_MODULE})
endfunction()
