if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "webgpu")
    message(WARNING "Python tutorials are not supported with WebGPU.")
    return()
endif ()

set(tests
    lesson_01_basics.py
    lesson_02_input_image.py
    lesson_03_debugging_1.py
    lesson_04_debugging_2.py
    lesson_05_scheduling_1.py
    lesson_06_realizing_over_shifted_domains.py
    lesson_07_multi_stage_pipelines.py
    lesson_08_scheduling_2.py
    lesson_09_update_definitions.py
    lesson_10_aot_compilation_generate.py
    lesson_10_aot_compilation_run.py
    lesson_11_cross_compilation.py
    lesson_12_using_the_gpu.py
    lesson_13_tuples.py
    lesson_14_types.py
    )

set(PYPATH_lesson_10_aot_compilation_run "$<TARGET_FILE_DIR:lesson_10_halide>")

foreach (test IN LISTS tests)
    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm" AND test MATCHES "lesson_10")
        message(WARNING "Not all tutorials build under WASM.")
        continue()
    endif ()

    cmake_path(GET test STEM test_name)
    add_python_test(
        FILE "${test}"
        LABEL python_tutorial
        PYTHONPATH "${PYPATH_${test_name}}"
    )
endforeach ()

if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    message(WARNING "Not all tutorials build under WASM.")
else ()
    ## Add some hacks for getting CMake to delay compiling lesson_10_halide until after the test has run. The "better" way
    ## of doing this might be to treat lesson 10 like an app and give it its own CMakeLists.txt, but since this is a one-off
    ## it is probably less maintenance work to do it like this.

    # Note that the following tests that are mentioned below were created in the above foreach loop:
    #   1. python_tutorial_lesson_10_aot_compilation_generate
    #   2. python_tutorial_lesson_10_aot_compilation_run
    # The test `python_tutorial_lesson_10_compile` below is responsible for running (1) in service of (2).

    # This dummy command "generates" the files that the TEST python_tutorial_lesson_10_aot_compilation_generate will
    # actually generate as part of the fixture below.
    add_custom_command(OUTPUT lesson_10_halide.py.cpp lesson_10_halide.o
                       COMMAND "${CMAKE_COMMAND}" -E echo Dummy command for lesson 10 sources.
                       VERBATIM)

    # This target allows CMake to build lesson_10_halide.so (or whatever the correct extension is) as part of the tests
    # later. It is excluded from ALL since it isn't valid to build outside of this context.
    Python3_add_library(lesson_10_halide MODULE EXCLUDE_FROM_ALL
                        lesson_10_halide.py.cpp
                        lesson_10_halide.o)

    target_link_libraries(lesson_10_halide PRIVATE Halide::Runtime)

    # Undocumented function in HalideGeneratorHelpers. Do not call in external code.
    # Users of the AOT functions (as opposed to Generators) should link to the relevant
    # GPU libraries manually.
    _Halide_target_link_gpu_libs(lesson_10_halide PRIVATE ${Halide_TARGET})

    # The fixture "py_lesson_10" orchestrates running the generator part of the lesson first, then the build for the
    # library, and finally runs python_tutorial_lesson_10_aot_compilation_run. The ..._compile test invokes CMake on
    # the current build for the above library.
    add_test(NAME python_tutorial_lesson_10_compile
             COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG> --target lesson_10_halide)

    set_tests_properties(python_tutorial_lesson_10_aot_compilation_generate PROPERTIES
                         FIXTURES_SETUP py_lesson_10)

    set_tests_properties(python_tutorial_lesson_10_compile PROPERTIES
                         FIXTURES_SETUP py_lesson_10
                         DEPENDS python_tutorial_lesson_10_aot_compilation_generate)

    set_tests_properties(python_tutorial_lesson_10_aot_compilation_run PROPERTIES
                         FIXTURES_REQUIRED py_lesson_10)
endif ()

##
# Packaging
##

include(GNUInstallDirs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/tutorial-python
        COMPONENT Halide_Documentation
        FILES_MATCHING PATTERN "*.py")
