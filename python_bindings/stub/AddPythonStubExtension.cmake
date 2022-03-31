set(HALIDE_PYSTUB_CPP_PATH ${CMAKE_CURRENT_LIST_DIR}/PyStub.cpp)

function(add_python_stub_extension TARGET)
    set(options)
    set(oneValueArgs GENERATOR MODULE)
    set(multiValueArgs SOURCES LINK_LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_GENERATOR)
        set(ARG_GENERATOR "${TARGET}")
    endif ()

    if (NOT ARG_MODULE)
        set(ARG_MODULE "${TARGET}_stub")
    endif ()

    Python3_add_library(${TARGET} MODULE ${HALIDE_PYSTUB_CPP_PATH} ${ARG_SOURCES})
    set_target_properties(${TARGET} PROPERTIES
                          CXX_VISIBILITY_PRESET hidden
                          VISIBILITY_INLINES_HIDDEN ON
                          POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${TARGET} PRIVATE
                               "HALIDE_PYSTUB_GENERATOR_NAME=${ARG_GENERATOR}"
                               "HALIDE_PYSTUB_MODULE_NAME=${ARG_MODULE}")
    target_link_libraries(${TARGET} PRIVATE Halide::PyStubs ${ARG_LINK_LIBRARIES})
    set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_MODULE})
endfunction()
