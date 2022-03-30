include(TargetExportScript)

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

set(GENERATORS
    addconstant
    bit
    complex
    simple
    user_context
    )

# Some Generators require extra Halide Target Features to be set.
set(FEATURES_user_context user_context)

# Some Generators have undefined types, sizes, etc that are useful for Stubs extensions,
# but unacceptable for AOT Extensions; ensure that all of those are explicitly
# specified for AOT. (We currently don't use or test these in AOT form, so the settings
# are somewhat arbitrary.)
set(GENPARAMS_complex
    array_input.size=2
    array_input.type=uint8
    int_arg.size=2
    simple_input.type=uint8
    untyped_buffer_input.type=uint8)

set(GENPARAMS_simple
    func_input.type=uint8)

# function(add_python_aot_extension TARGET)
#     set(options)
#     set(oneValueArgs GENERATOR MODULE)
#     set(multiValueArgs)
#     cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

#     if (NOT ARG_GENERATOR)
#         set(ARG_GENERATOR "${TARGET}")
#     endif ()

#     if (NOT ARG_MODULE)
#         set(ARG_MODULE "${TARGET}_stub")
#     endif ()

#     Python3_add_library(${TARGET} MODULE ${HALIDE_PYSTUB_CPP_PATH})
#     target_compile_definitions(${TARGET} PRIVATE
#                                "HALIDE_PYSTUB_GENERATOR_NAME=${ARG_GENERATOR}"
#                                "HALIDE_PYSTUB_MODULE_NAME=${ARG_MODULE}")
#     target_link_libraries(${TARGET} PRIVATE Halide::PyStubs)
#     set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${ARG_MODULE})
# endfunction()

foreach (GEN IN LISTS GENERATORS)
    # Create the Halide generator executable. (Prepend "Py_" to avoid name
    # conflicts with stuff elsewhere in Halide CMake files.)
    add_executable(Py_${GEN}.generator ${GEN}_generator.cpp)
    target_link_libraries(Py_${GEN}.generator PRIVATE Halide::Generator)

    # Run the Generator to produce a static library of AOT code,
    # plus the 'python_extension' code necessary to produce a useful
    # AOT Extention for Python:
    add_halide_library(aot_${GEN}
                       FROM Py_${GEN}.generator
                       GENERATOR ${GEN}
                       FUNCTION_NAME ${GEN}
                       PYTHON_EXTENSION ${GEN}.py.cpp
                       FEATURES ${FEATURES_${GEN}}
                       PARAMS ${GENPARAMS_${GEN}}
                       TARGETS cmake)

    # Take the native-code output of the Generator, add the Python-Extension
    # code (to make it callable from Python), and build it into the AOT Extension we need.
    Python3_add_library(py_aot_${GEN} MODULE ${${GEN}.py.cpp})
    target_link_libraries(py_aot_${GEN} PRIVATE aot_${GEN})
    set_target_properties(py_aot_${GEN} PROPERTIES OUTPUT_NAME ${GEN}) # Python3_add_library adds target info to name.

    # Construct linker script that will export *just* the PyInit entry
    # point we want. (If we don't do this we can have interesting failures
    # when loading multiple of these Python extensions in the same space.)
    #
    # TODO: How to do this for Windows as well?
    configure_file(ext.ldscript.apple.in "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript.apple")
    configure_file(ext.ldscript.linux.in "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript")
    target_export_script(
        py_aot_${GEN}
        APPLE_LD "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript.apple"
        GNU_LD "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript"
    )

    # OK, now we want to produce a Stub Extension for the same Generator:
    # Compiling PyStub.cpp, then linking with the generator's .o file, PyStubImpl.o,
    # plus the same libHalide being used by halide.so.
    #
    # Note that we set HALIDE_PYSTUB_MODULE_NAME to $*_stub (e.g. foo_stub) but
    # set HALIDE_PYSTUB_GENERATOR_NAME to the unadorned name of the Generator.
    add_python_stub_extension(py_stub_${GEN}
                              GENERATOR ${GEN}
                              MODULE ${GEN}_stub)

    # Same trick with Linker scripts here.
    configure_file(ext.ldscript.apple.in "${CMAKE_CURRENT_BINARY_DIR}/${GEN}_stub.ldscript.apple")
    configure_file(ext.ldscript.linux.in "${CMAKE_CURRENT_BINARY_DIR}/${GEN}_stub.ldscript")
    target_export_script(
        py_stub_${GEN}
        APPLE_LD "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript.apple"
        GNU_LD "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript"
    )


endforeach ()
