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

    list(GET ARG_GEN 0 GEN_NAME)
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

    foreach (G IN LISTS ARG_GEN)
        add_halide_library(py_aot_${G}
                           FROM ${GEN_TARGET}
                           GENERATOR ${G}
                           FUNCTION_NAME ${G}
                           FEATURES ${FEATURES_${G}}
                           PARAMS ${GENPARAMS_${G}}
                           # Note that PYTHON_EXTENSION_LIBRARY doesn't take
                           # an argument; it will always produce a file of the form
                           # ${TARGET}.${Python3_SOABI}.so (or .pyd on Windows)
                           PYTHON_EXTENSION_LIBRARY
                           # We don't really need all the plugins at once here --
                           # it's just easier to specify them all
                           PLUGINS Halide::Adams2019 Halide::Li2018 Halide::Mullapudi2016)
        set(_ALL_DEPS "${_ALL_DEPS} py_aot_${G}" PARENT_SCOPE)
    endforeach()
endfunction()

_add_python_aot_and_stub_extension(SOURCES addconstant_generator.cpp
                                   GEN addconstant
                                       addconstant_with_offset_42
                                       addconstant_with_negative_offset)
_add_python_aot_and_stub_extension(SOURCES bit_generator.cpp GEN bit)
_add_python_aot_and_stub_extension(SOURCES complex_generator.cpp GEN complex)
_add_python_aot_and_stub_extension(SOURCES simple_generator.cpp GEN simple)
_add_python_aot_and_stub_extension(SOURCES user_context_generator.cpp GEN user_context)

_add_python_aot_and_stub_extension(SOURCES addconstantpy_generator.py
                                   GEN addconstantpy
                                       addconstantpy_with_offset_42
                                       addconstantpy_with_negative_offset)
_add_python_aot_and_stub_extension(SOURCES bilateral_grid_generator.py
                                   GEN bilateral_grid
                                       bilateral_grid_Adams2019
                                       bilateral_grid_Li2018
                                       bilateral_grid_Mullapudi2016)
_add_python_aot_and_stub_extension(SOURCES bitpy_generator.py GEN bitpy)
_add_python_aot_and_stub_extension(SOURCES complexpy_generator.py GEN complexpy)
_add_python_aot_and_stub_extension(SOURCES simplepy_generator.py GEN simplepy)

set(PYTHON_CORRECTNESS_GENERATOR_DEPS ${_ALL_DEPS} PARENT_SCOPE)
