
include(PythonExtensionHelpers)

# Some Generators require extra Halide Target Features to be set.
set(FEATURES_user_context user_context)

# Some Generators have undefined types, sizes, etc that are useful for Stubs extensions,
# but unacceptable for AOT Extensions; ensure that all of those are explicitly
# specified for AOT. (We currently don't use or test these in AOT form, so the settings
# are somewhat arbitrary.)
set(GENPARAMS_complex
    simple_input.type=uint8
    tuple_output.type=float32,float32
    untyped_buffer_input.type=uint8
    untyped_buffer_output.type=uint8
    untyped_buffer_output.dim=3)

set(GENPARAMS_complexpy ${GENPARAMS_complex})

set(_ALL_DEPS)

function(_add_python_aot_and_stub_extension)
    cmake_parse_arguments(ARG "" "" "GEN;SOURCES" ${ARGN})
    foreach (G IN LISTS ARG_GEN)
        add_python_aot_extension(py_aot_${G}
                                 GENERATOR ${G}
                                 FEATURES ${FEATURES_${G}}
                                 PARAMS ${GENPARAMS_${G}}
                                 SOURCES "${ARG_SOURCES}")
        add_python_stub_extension(py_stub_${G}
                                  SOURCES "${ARG_SOURCES}"
                                  GENERATOR ${G}
                                  MODULE ${G}_stub)

        set(_ALL_DEPS "${_ALL_DEPS} py_aot_${G} py_stub_${G}" PARENT_SCOPE)
    endforeach()
endfunction()

_add_python_aot_and_stub_extension(SOURCES addconstant_generator.cpp
                                   GEN addconstant addconstant_with_offset_42 addconstant_with_negative_offset)
_add_python_aot_and_stub_extension(SOURCES bit_generator.cpp GEN bit)
_add_python_aot_and_stub_extension(SOURCES complex_generator.cpp GEN complex)
_add_python_aot_and_stub_extension(SOURCES simple_generator.cpp GEN simple)
_add_python_aot_and_stub_extension(SOURCES user_context_generator.cpp GEN user_context)

_add_python_aot_and_stub_extension(SOURCES addconstantpy_generator.py
                                   GEN addconstantpy addconstantpy_with_offset_42 addconstantpy_with_negative_offset)
_add_python_aot_and_stub_extension(SOURCES bitpy_generator.py GEN bitpy)
_add_python_aot_and_stub_extension(SOURCES complexpy_generator.py GEN complexpy)
_add_python_aot_and_stub_extension(SOURCES simplepy_generator.py GEN simplepy)

set(PYTHON_CORRECTNESS_GENERATOR_DEPS ${_ALL_DEPS} PARENT_SCOPE)
