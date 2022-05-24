include(PythonExtensionHelpers)

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

foreach (GEN IN LISTS GENERATORS)
    add_python_aot_extension(py_aot_${GEN}
                             GENERATOR ${GEN}
                             FEATURES ${FEATURES_${GEN}}
                             PARAMS ${GENPARAMS_${GEN}}
                             SOURCES ${GEN}_generator.cpp)

    add_python_stub_extension(py_stub_${GEN}
                              SOURCES ${GEN}_generator.cpp
                              GENERATOR ${GEN}
                              MODULE ${GEN}_stub)
endforeach ()
