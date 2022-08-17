cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/HalideGeneratorHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TargetExportScript.cmake)

set(_STUB_DIR "${Halide_SOURCE_DIR}/python_bindings/stub")


function(_target_export_single_symbol TARGET SYMBOL)
    file(WRITE
         "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript.apple"
         "_${SYMBOL}\n")
    file(WRITE
         "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript"
         "{ global: ${SYMBOL}; local: *; };\n")
    target_export_script(
        ${TARGET}
        APPLE_LD "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript.apple"
        GNU_LD "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript"
    )
endfunction()

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

    # Produce a Stub Extension for the same Generator:
    # Compiling PyStub.cpp, then linking with the generator's .o file, PyStubImpl.o,
    # plus the same libHalide being used by halide.so.
    #
    # Note that we set HALIDE_PYSTUB_MODULE_NAME to $*_stub (e.g. foo_stub) but
    # set HALIDE_PYSTUB_GENERATOR_NAME to the unadorned name of the Generator.
    Python3_add_library(${TARGET} MODULE WITH_SOABI ${_STUB_DIR}/PyStub.cpp ${ARG_SOURCES})
    set_target_properties(${TARGET} PROPERTIES
                          CXX_VISIBILITY_PRESET hidden
                          VISIBILITY_INLINES_HIDDEN ON
                          POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${TARGET} PRIVATE
                               "HALIDE_PYSTUB_GENERATOR_NAME=${ARG_GENERATOR}"
                               "HALIDE_PYSTUB_MODULE_NAME=${ARG_MODULE}")
    target_link_libraries(${TARGET} PRIVATE Halide::PyStubs ${ARG_LINK_LIBRARIES})
    set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_MODULE})
    _target_export_single_symbol(${TARGET} "PyInit_${ARG_MODULE}")
endfunction()
