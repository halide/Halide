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
    untyped_buffer_input.type=uint8
    untyped_buffer_output.type=uint8)

set(GENPARAMS_simple
    func_input.type=uint8)

# Since simple and user_context are going to be bound into a single
# Python extension library, they must share the same Halide runtime.
# Note that by leaving out the TARGETS argument, we default to whatever
# ${Halide_TARGET} was set to at configure time.
add_halide_runtime(multi_lib_runtime)

set(RUNTIME_simple multi_lib_runtime)
set(RUNTIME_user_context multi_lib_runtime)

foreach (GEN IN LISTS GENERATORS)
    add_halide_generator(py_gen_${GEN}.generator
                         SOURCES ${GEN}_generator.cpp
                         PYSTUB ${GEN})

    add_halide_library(py_aot_${GEN}
                       FROM py_gen_${GEN}.generator
                       GENERATOR ${GEN}
                       FUNCTION_NAME ${GEN}
                       USE_RUNTIME ${RUNTIME_${GEN}}
                       PYTHON_EXTENSION _ignored_result
                       FEATURES ${FEATURES_${GEN}} c_plus_plus_name_mangling
                       PARAMS ${GENPARAMS_${GEN}})

    add_halide_python_extension_library(pyext_${GEN}
                                        MODULE_NAME ${GEN}
                                        HALIDE_LIBRARIES py_aot_${GEN})

endforeach ()

# Bind several libraries into a single python extension;
# they will be in the same module (and share the same Halide runtime)
add_halide_python_extension_library(pyext_multi_method_module
                                    MODULE_NAME multi_method_module
                                    HALIDE_LIBRARIES py_aot_simple py_aot_user_context)

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
