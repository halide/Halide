# Some Generators require extra Halide Target Features to be set.
set(FEATURES_user_context user_context)

# Some Generators have undefined types, sizes, etc that are useful for Stubs extensions,
# but unacceptable for AOT Extensions; ensure that all of those are explicitly
# specified for AOT. (We currently don't use or test these in AOT form, so the settings
# are somewhat arbitrary.)
set(GENPARAMS_complexcpp
    simple_input.type=uint8
    tuple_output.type=float32,float32
    untyped_buffer_input.type=uint8
    untyped_buffer_output.type=uint8
    untyped_buffer_output.dim=3)

set(GENPARAMS_complexpy ${GENPARAMS_complexcpp})

# Since simplecpp and user_context are going to be bound into a single
# Python extension library, they must share the same Halide runtime.
# Note that by leaving out the TARGETS argument, we default to whatever
# ${Halide_TARGET} was set to at configure time.
add_halide_runtime(multi_lib_runtime)

set(RUNTIME_simplecpp multi_lib_runtime)
set(RUNTIME_user_context multi_lib_runtime)

add_custom_target(PythonCorrectnessGenerators)

function(_add_python_aot_and_stub_extension)
    cmake_parse_arguments(ARG "" "" "GENERATORS;SOURCES" ${ARGN})

    list(GET ARG_GENERATORS 0 GEN_NAME)
    set(GEN_TARGET py_gen_${GEN_NAME})

    # It is illegal to specify PYSTUB for a Python Generator
    if (ARG_SOURCES MATCHES ".py$")
        add_halide_generator(${GEN_TARGET}
                             SOURCES ${ARG_SOURCES})
    else ()
        add_halide_generator(${GEN_TARGET}
                             PYSTUB ${GEN_NAME}
                             SOURCES ${ARG_SOURCES})
    endif ()

    foreach (G IN LISTS ARG_GENERATORS)
        add_halide_library(py_aot_${G}
                           FROM ${GEN_TARGET}
                           GENERATOR ${G}
                           FUNCTION_NAME ${G}
                           USE_RUNTIME ${RUNTIME_${G}}
                           PYTHON_EXTENSION _ignored_result
                           FEATURES ${FEATURES_${G}}
                           PARAMS ${GENPARAMS_${G}}
                           # We don't really need all the plugins at once here --
                           # it's just easier to specify them all
                           PLUGINS Halide::Adams2019 Halide::Li2018 Halide::Mullapudi2016)

        add_halide_python_extension_library(pyext_${G}
                                            MODULE_NAME ${G}
                                            HALIDE_LIBRARIES py_aot_${G})

        add_dependencies(PythonCorrectnessGenerators "py_aot_${G}" "pyext_${G}")
    endforeach()
endfunction()

_add_python_aot_and_stub_extension(SOURCES addconstantcpp_generator.cpp
                                   GENERATORS addconstantcpp
                                              addconstantcpp_with_offset_42
                                              addconstantcpp_with_negative_offset)
_add_python_aot_and_stub_extension(SOURCES bitcpp_generator.cpp GENERATORS bitcpp)
_add_python_aot_and_stub_extension(SOURCES complexcpp_generator.cpp GENERATORS complexcpp)
_add_python_aot_and_stub_extension(SOURCES simplecpp_generator.cpp GENERATORS simplecpp)
_add_python_aot_and_stub_extension(SOURCES user_context_generator.cpp GENERATORS user_context)

_add_python_aot_and_stub_extension(SOURCES addconstantpy_generator.py
                                   GENERATORS addconstantpy
                                              addconstantpy_with_offset_42
                                              addconstantpy_with_negative_offset)
_add_python_aot_and_stub_extension(SOURCES bitpy_generator.py GENERATORS bitpy)
_add_python_aot_and_stub_extension(SOURCES complexpy_generator.py GENERATORS complexpy)
_add_python_aot_and_stub_extension(SOURCES simplepy_generator.py GENERATORS simplepy)

# Bind several libraries into a single python extension;
# they will be in the same module (and share the same Halide runtime)
add_halide_python_extension_library(pyext_multi_method_module
                                    MODULE_NAME multi_method_module
                                    HALIDE_LIBRARIES py_aot_simplecpp py_aot_user_context)
add_dependencies(PythonCorrectnessGenerators "pyext_multi_method_module")

# Add a test to verify that Python extensions export exactly one symbol (the PyInit symbol)
# TODO: _Halide_target_export_single_symbol() isn't implemented for MSVC, and even if it was,
# this test wouldn't work for it, but for Posixy systems (Linux, OSX, etc) this is fine.
# See https://github.com/halide/Halide/issues/6982
if(NOT MSVC)
    # Verify that only one symbol is exported, and that symbol is the one we expect
    add_test(NAME test_export_single_symbol
             COMMAND $ENV{SHELL} -c "[ $(nm $<TARGET_FILE:pyext_multi_method_module> | grep ' T ' | wc -l) -eq 1 ] && \
                                     [ $(nm $<TARGET_FILE:pyext_multi_method_module> | grep ' T ' | grep PyInit_multi_method_module | wc -l) -eq 1 ]"
             VERBATIM)
endif ()
