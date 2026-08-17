if (Halide_TARGET MATCHES "webgpu")
    message(WARNING "Python tutorials are not supported with WebGPU.")
    return()
endif ()

# None of this directory's targets should build as part of a plain
# `cmake --build .` -- tutorial code should only run when its own test asks
# for it (mirroring the intent of lesson 10's EXCLUDE_FROM_ALL below, applied
# directory-wide so lessons 15/16/21's add_halide_library()-based targets get
# the same treatment without needing the same manual dummy-command dance).
set_property(DIRECTORY PROPERTY EXCLUDE_FROM_ALL TRUE)  # nolint: no per-target alternative that covers add_halide_library()'s several targets per lesson

set(tests
    # keep-sorted start
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
    lesson_17_predicated_rdom.py
    lesson_18_parallel_associative_reductions.py
    lesson_19_wrapper_funcs.py
    lesson_20_cloning_funcs.py
    lesson_22_jit_performance.py
    lesson_23_serialization.py
    lesson_24_async.py
    # keep-sorted end
)

set(PYPATH_lesson_10_aot_compilation_run "$<TARGET_FILE_DIR:lesson_10_halide>")

foreach (test IN LISTS tests)
    if (Halide_TARGET MATCHES "wasm" AND test MATCHES "lesson_10")
        message(WARNING "Not all tutorials build under WASM.")
        continue()
    endif ()

    if (Halide_TARGET MATCHES "vulkan" AND test MATCHES "lesson_10")
        message(WARNING "Not all tutorials build under Vulkan.")
        continue()
    endif ()

    cmake_path(GET test STEM test_name)
    add_python_test(FILE "${test}" LABEL python_tutorial PYTHONPATH "${PYPATH_${test_name}}")
endforeach ()

if (Halide_TARGET MATCHES "wasm")
    message(WARNING "Not all tutorials build under WASM.")
elseif (Halide_TARGET MATCHES "vulkan")
    message(WARNING "Not all tutorials build under Vulkan.")
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
    add_custom_command(
        OUTPUT
            lesson_10_halide.py.cpp
            lesson_10_halide.o
        COMMAND "${CMAKE_COMMAND}" -E echo Dummy command for lesson 10 sources.
        VERBATIM
    )

    # This target allows CMake to build lesson_10_halide.so (or whatever the correct extension is) as part of the tests
    # later. It is excluded from ALL since it isn't valid to build outside of this context.
    Python_add_library(
        lesson_10_halide
        MODULE
        EXCLUDE_FROM_ALL lesson_10_halide.py.cpp lesson_10_halide.o
    )

    # TODO: the EXCLUDE_FROM_ALL above is a bit of a hack - even a normal build won't
    #   prepare this file for linting. Disable export of compile commands for this target.
    set_target_properties(lesson_10_halide PROPERTIES EXPORT_COMPILE_COMMANDS OFF)

    target_link_libraries(lesson_10_halide PRIVATE Halide::Runtime)

    # Undocumented function in HalideGeneratorHelpers. Do not call in external code.
    # Users of the AOT functions (as opposed to Generators) should link to the relevant
    # GPU libraries manually.
    _Halide_target_link_gpu_libs(lesson_10_halide PRIVATE ${Halide_TARGET})

    # The fixture "py_lesson_10" orchestrates running the generator part of the lesson first, then the build for the
    # library, and finally runs python_tutorial_lesson_10_aot_compilation_run. The ..._compile test invokes CMake on
    # the current build for the above library.
    add_test(
        NAME python_tutorial_lesson_10_compile
        COMMAND
            "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG>
            --target lesson_10_halide
    )

    set_tests_properties(
        python_tutorial_lesson_10_aot_compilation_generate
        PROPERTIES
        FIXTURES_SETUP py_lesson_10
    )

    set_tests_properties(
        python_tutorial_lesson_10_compile
        PROPERTIES
        FIXTURES_SETUP py_lesson_10
        DEPENDS python_tutorial_lesson_10_aot_compilation_generate
    )

    set_tests_properties(
        python_tutorial_lesson_10_aot_compilation_run
        PROPERTIES
        FIXTURES_REQUIRED py_lesson_10
    )

    ## Lessons 15, 16, and 21 use Generator classes, so unlike the plain
    ## scripts above, they need add_halide_generator()/add_halide_library()
    ## to actually run the generator (mirroring tutorial/CMakeLists.txt's
    ## C++ versions and python_bindings/apps/CMakeLists.txt's use of the same
    ## machinery). The directory-wide EXCLUDE_FROM_ALL above keeps these
    ## targets from building as a side effect of a plain `cmake --build .`;
    ## each lesson's own test explicitly builds what it needs (via `cmake
    ## --build --target`), the same trick lesson 10 uses above, just without
    ## needing a dummy placeholder command since these go through the real
    ## generator machinery. Target names are prefixed with `python_` to
    ## avoid colliding with the identically-named C++ targets that
    ## tutorial/CMakeLists.txt already defines for the same lessons.

    # Lesson 15: two Generators, with no companion "run" script (matching
    # the C++ source, which only builds the generators and checks that the
    # expected outputs exist).
    add_halide_generator(python_gen_lesson_15 SOURCES lesson_15_generators.py)

    add_halide_library(
        python_my_first_generator
        FROM python_gen_lesson_15
        GENERATOR my_first_generator
    )

    add_halide_library(
        python_my_second_generator_1
        FROM python_gen_lesson_15
        GENERATOR my_second_generator
        PARAMS parallel=false scale=3.0 rotation=ccw output.type=uint16
    )
    add_halide_library(
        python_my_second_generator_2
        FROM python_gen_lesson_15
        GENERATOR my_second_generator
        PARAMS scale=9.0 rotation=ccw output.type=float32
    )
    add_halide_library(
        python_my_second_generator_3
        FROM python_gen_lesson_15
        GENERATOR my_second_generator
        PARAMS parallel=false output.type=float64
    )

    add_custom_target(python_lesson_15_targets)
    add_dependencies(
        python_lesson_15_targets
        python_my_first_generator
        python_my_second_generator_1
        python_my_second_generator_2
        python_my_second_generator_3
    )

    add_test(
        NAME python_tutorial_lesson_15_build_gens
        COMMAND
            "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target python_lesson_15_targets
            --config $<CONFIG>
    )
    set_tests_properties(python_tutorial_lesson_15_build_gens PROPERTIES LABELS python_tutorial)

    # Lesson 16: one Generator compiled 4 ways (one per memory layout), each
    # wrapped as its own importable Python extension module so that
    # lesson_16_rgb_run.py can exercise all four.
    add_halide_generator(python_gen_lesson_16 SOURCES lesson_16_rgb_generate.py)

    foreach (layout IN ITEMS planar interleaved either specialized)
        add_halide_library(
            python_brighten_${layout}
            FROM python_gen_lesson_16
            GENERATOR brighten
            FUNCTION_NAME brighten_${layout}
            PARAMS layout=${layout}
            PYTHON_EXTENSION _ignored_result
        )
        add_halide_python_extension_library(
            python_ext_brighten_${layout}
            MODULE_NAME brighten_${layout}
            HALIDE_LIBRARIES python_brighten_${layout}
        )
    endforeach ()

    add_python_test(
        FILE lesson_16_rgb_run.py
        LABEL python_tutorial
        PYTHONPATH "$<TARGET_FILE_DIR:python_ext_brighten_planar>"
    )

    add_test(
        NAME python_tutorial_lesson_16_compile
        COMMAND
            "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG>
            --target
            python_ext_brighten_planar python_ext_brighten_interleaved
            python_ext_brighten_either python_ext_brighten_specialized
    )
    set_tests_properties(
        python_tutorial_lesson_16_compile
        PROPERTIES
        LABELS python_tutorial
        FIXTURES_SETUP py_lesson_16
    )
    set_tests_properties(
        python_tutorial_lesson_16_rgb_run
        PROPERTIES
        FIXTURES_REQUIRED py_lesson_16
    )

    # Lesson 21: one Generator compiled with and without the Mullapudi2016
    # autoscheduler, matching the C++ source's own guard on that target's
    # availability.
    if (TARGET Halide::Mullapudi2016)
        add_halide_generator(python_gen_lesson_21 SOURCES lesson_21_auto_scheduler_generate.py)

        add_halide_library(
            python_auto_schedule_false
            FROM python_gen_lesson_21
            GENERATOR auto_schedule_gen
            FUNCTION_NAME auto_schedule_false
            PYTHON_EXTENSION _ignored_result
        )
        add_halide_library(
            python_auto_schedule_true
            FROM python_gen_lesson_21
            GENERATOR auto_schedule_gen
            FUNCTION_NAME auto_schedule_true
            AUTOSCHEDULER Halide::Mullapudi2016
            PARAMS
                autoscheduler.parallelism=32
                autoscheduler.last_level_cache_size=16777216
                autoscheduler.balance=40
            PYTHON_EXTENSION _ignored_result
        )

        add_halide_python_extension_library(
            python_ext_auto_schedule_false
            MODULE_NAME auto_schedule_false
            HALIDE_LIBRARIES python_auto_schedule_false
        )
        add_halide_python_extension_library(
            python_ext_auto_schedule_true
            MODULE_NAME auto_schedule_true
            HALIDE_LIBRARIES python_auto_schedule_true
        )

        add_python_test(
            FILE lesson_21_auto_scheduler_run.py
            LABEL python_tutorial
            PYTHONPATH "$<TARGET_FILE_DIR:python_ext_auto_schedule_false>"
        )

        add_test(
            NAME python_tutorial_lesson_21_compile
            COMMAND
                "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG>
                --target python_ext_auto_schedule_false python_ext_auto_schedule_true
        )
        set_tests_properties(
            python_tutorial_lesson_21_compile
            PROPERTIES
            LABELS python_tutorial
            FIXTURES_SETUP py_lesson_21
        )
        set_tests_properties(
            python_tutorial_lesson_21_auto_scheduler_run
            PROPERTIES
            FIXTURES_REQUIRED py_lesson_21
        )
    endif ()
endif ()
