include(HalideGeneratorHelpers)
include(TargetExportScript)

set(_STUB_DIR "${Halide_SOURCE_DIR}/python_bindings/stub")

# There are two sorts of Python Extensions that we can produce for a Halide Generator
# written in C++:
#
# - One that is essentially the 'native code' output of a Generator, wrapped with enough CPython
#   glue code to make it callable from Python. This is analogous to the usual Generator output
#   when building a C++ codebase, and is the usual mode used for distribution of final product;
#   these correspond to 'ahead-of-time' (AOT) code generation. The resulting code has no dependency
#   on libHalide. We'll refer to this sort of extension as an "AOT extension".
#
# - One that essentially *the Generator itself*, wrapped in CPython glue code to make it callable
#   from Python at Halide compilation time. This is analogous to the (rarely used) GeneratorStub
#   code that can be used to compose multiple Generators together. The resulting extension *does*
#   depend on libHalide, and can be used in either JIT or AOT mode for compilation.
#   We'll refer to this sort of extension as a "Stub extension".
#
# For testing purposes here, we don't bother using distutils/setuptools to produce a properly-packaged
# Python extension; rather, we simply produce a .so file with the correct name exported, and ensure
# it's in the PYTHONPATH when testing.
#
# In our build files here, we build both kinds of extension for every Generator in the generators/
# directory (even though not all are used). As a simplistic way to distinguish between the two
# sorts of extensions, we use the unadorned Generator name for AOT extensions, and the Generator name
# suffixed with "_stub" for Stub extensions. (TODO: this is unsatisfyingly hackish; better suggestions
# would be welcome.)

function(target_export_single_symbol TARGET SYMBOL)
    configure_file("${_STUB_DIR}/ext.ldscript.apple.in" "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript.apple")
    configure_file("${_STUB_DIR}/ext.ldscript.linux.in" "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript")
    target_export_script(
        ${TARGET}
        APPLE_LD "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript.apple"
        GNU_LD "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.ldscript"
    )
endfunction()

function(add_python_aot_extension TARGET)
    set(options)
    set(oneValueArgs GENERATOR FUNCTION_NAME)
    set(multiValueArgs SOURCES LINK_LIBRARIES FEATURES PARAMS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_GENERATOR)
        set(ARG_GENERATOR "${TARGET}")
    endif ()

    if (NOT ARG_FUNCTION_NAME)
        set(ARG_FUNCTION_NAME "${ARG_GENERATOR}")
    endif ()

    # Create the Halide generator executable.
    add_executable(${TARGET}.generator ${ARG_SOURCES})
    target_link_libraries(${TARGET}.generator PRIVATE Halide::Generator ${ARG_LINK_LIBRARIES})

    # TODO: this should work (and would be preferred to the code above)
    # but CMake fails with "targets not yet defined"; investigate.
    # add_halide_generator(${TARGET}.generator
    #                      SOURCES ${ARG_SOURCES})

    # Run the Generator to produce a static library of AOT code,
    # plus the 'python_extension' code necessary to produce a useful
    # AOT Extention for Python:
    add_halide_library(aot_${TARGET}
                       FROM ${TARGET}.generator
                       GENERATOR ${ARG_GENERATOR}
                       FUNCTION_NAME ${ARG_FUNCTION_NAME}
                       PYTHON_EXTENSION ${TARGET}.py.cpp
                       FEATURES ${ARG_FEATURES}
                       PARAMS ${ARG_PARAMS}
                       TARGETS cmake)

    # Take the native-code output of the Generator, add the Python-Extension
    # code (to make it callable from Python), and build it into the AOT Extension we need.
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
        # Add soabi info (like cpython-310-x86_64-linux-gnu)
        # when CMake is new enough to know how to do it.
        set(abi_flags WITH_SOABI)
    else ()
        set(abi_flags "")
    endif ()

    Python3_add_library(${TARGET} MODULE ${abi_flags} ${${TARGET}.py.cpp})
    target_link_libraries(${TARGET} PRIVATE aot_${TARGET})
    set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_FUNCTION_NAME})
    target_export_single_symbol(${TARGET} ${ARG_FUNCTION_NAME})
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
    Python3_add_library(${TARGET} MODULE ${_STUB_DIR}/PyStub.cpp ${ARG_SOURCES})
    set_target_properties(${TARGET} PROPERTIES
                          CXX_VISIBILITY_PRESET hidden
                          VISIBILITY_INLINES_HIDDEN ON
                          POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${TARGET} PRIVATE
                               "HALIDE_PYSTUB_GENERATOR_NAME=${ARG_GENERATOR}"
                               "HALIDE_PYSTUB_MODULE_NAME=${ARG_MODULE}")
    target_link_libraries(${TARGET} PRIVATE Halide::PyStubs ${ARG_LINK_LIBRARIES})
    set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_MODULE})
    target_export_single_symbol(${TARGET} ${ARG_MODULE})
endfunction()
