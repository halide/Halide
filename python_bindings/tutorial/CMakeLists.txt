test_group(tutorial lesson_01_basics)
test_group(tutorial lesson_02_input_image)
test_group(tutorial lesson_03_debugging_1)
test_group(tutorial lesson_04_debugging_2)
test_group(tutorial lesson_05_scheduling_1)
test_group(tutorial lesson_06_realizing_over_shifted_domains)
test_group(tutorial lesson_07_multi_stage_pipelines)
test_group(tutorial lesson_08_scheduling_2)
test_group(tutorial lesson_09_update_definitions)
test_group(tutorial lesson_10_aot_compilation_generate)

if (MSVC)
  add_test(NAME "python_bindings_tutorial_lesson_10_halide_compile"
           COMMAND "${CMAKE_CXX_COMPILER}"
                   ${HALIDE_PY_RTTI_FLAG}
                   "/NOLOGO"
                   "/c"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py.cpp"
                   "${CMAKE_INCLUDE_FLAG_C}${PYTHON_INCLUDE_DIR}"
                   "${CMAKE_INCLUDE_FLAG_C}${HALIDE_INCLUDE_DIR}")
  add_test(NAME "python_bindings_tutorial_lesson_10_halide"
           COMMAND "${CMAKE_LINKER}"
                   ${HALIDE_PY_LINK_FLAG}
                   "/NOIMPLIB"
                   "/DLL"
                   "/NOLOGO"
                   "/OUT:$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>/lesson_10_halide${PYTHON_MODULE_EXTENSION}"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py${CMAKE_CXX_OUTPUT_EXTENSION}"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.o"
                   "${PYTHON_LIBRARY}")
  set_property(TEST "python_bindings_tutorial_lesson_10_halide"
               PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_halide_compile")
  set_property(TEST "python_bindings_tutorial_lesson_10_halide_compile"
               PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_aot_compilation_generate")
else()
  if (APPLE)
    set(EXTRA_FLAG "-bundle" "-Wl,-headerpad_max_install_names"
                   "${CMAKE_CXX_SYSROOT_FLAG}" "${CMAKE_OSX_SYSROOT}")
  else()
    set(EXTRA_FLAG "-shared")
  endif()
  add_test(NAME "python_bindings_tutorial_lesson_10_halide"
           COMMAND "${CMAKE_CXX_COMPILER}"
                   ${EXTRA_FLAG}
                   "${CMAKE_SHARED_MODULE_CXX_FLAGS}"
                   ${HALIDE_PY_LINK_FLAG}
                   ${HALIDE_PY_RTTI_FLAG}
                   "-o"
                   "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lesson_10_halide${CMAKE_SHARED_MODULE_SUFFIX}"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py.cpp"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.o"
                   "${CMAKE_INCLUDE_FLAG_C}${PYTHON_INCLUDE_DIR}"
                   "${CMAKE_INCLUDE_FLAG_C}${HALIDE_INCLUDE_DIR}")
  set_property(TEST "python_bindings_tutorial_lesson_10_halide"
               PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_aot_compilation_generate")
endif()

test_group(tutorial lesson_10_aot_compilation_run)
test_group(tutorial lesson_11_cross_compilation)
test_group(tutorial lesson_12_using_the_gpu)
test_group(tutorial lesson_13_tuples)
test_group(tutorial lesson_14_types)

set_property(TEST "python_bindings_tutorial_lesson_10_aot_compilation_run"
             PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_halide")
