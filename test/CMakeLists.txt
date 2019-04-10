option(WITH_TEST_INTERNAL "Build and run internal tests" ON)
option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
option(WITH_TEST_ERROR "Build error tests" ON)
option(WITH_TEST_WARNING "Build warning tests" ON)
option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
option(WITH_TEST_OPENGL "Build OpenGL tests" ON)
option(WITH_TEST_GENERATOR "Build Generator tests" ON)
option(WITH_TEST_AUTO_SCHEDULE "Build auto_schedule tests" ON)

if (WITH_TEST_INTERNAL)
  message(STATUS "Internal tests enabled")
  halide_project(test_internal internal internal.cpp)
  add_test(test_internal
           GROUPS run_tests)
else()
  message(WARNING "Internal tests disabled")
endif()

function(test_plain_c_includes)
  set(folder "correctness")
  set(name "plain_c_includes")
  set(TEST_NAMES "${folder}_${name}")
  add_executable("${folder}_${name}" "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/${name}.c")
  target_include_directories("${folder}_${name}" PRIVATE "${CMAKE_BINARY_DIR}/include")
  set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction(test_plain_c_includes)

function(tests folder)
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${folder}" "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp")
  set(TEST_NAMES "")
  foreach(file ${TESTS})
    string(REPLACE ".cpp" "" name "${file}")

    set(TARGET "${folder}_${name}")
    list(APPEND TEST_NAMES "${TARGET}")

    halide_project("${TARGET}" "${folder}" "${folder}/${file}")
    target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_definitions("${TARGET}"  PRIVATE "-DLLVM_VERSION=${LLVM_VERSION}")

    set(GROUPS "test_${folder}")
    if("${folder}" STREQUAL "performance" OR "${folder}" STREQUAL "auto_schedule")
      # These shouldn't be part of run_tests, since they must be run with -j for timing purposes
    else()
      list(APPEND GROUPS run_tests)
    endif()
    if("${folder}" STREQUAL "error")
      add_test("${TARGET}"
               GROUPS ${GROUPS}
               EXPECT_FAILURE)
    else()
      add_test("${TARGET}"
               GROUPS ${GROUPS})
    endif()

  endforeach()
  set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction(tests)

if (WITH_TEST_AUTO_SCHEDULE)
  tests(auto_schedule)
endif()
if (WITH_TEST_CORRECTNESS)
  tests(correctness)
  halide_use_image_io(correctness_image_io)
  test_plain_c_includes()
endif()
if (WITH_TEST_ERROR)
  tests(error)
endif()
if (WITH_TEST_WARNING)
  tests(warning)
endif()
if (WITH_TEST_PERFORMANCE)
  tests(performance)
endif()
if (WITH_TEST_OPENGL)
  # Use vendor libraries even when legacy libs are also available
  # see: https://cmake.org/cmake/help/git-stage/policy/CMP0072.html
  set(OpenGL_GL_PREFERENCE GLVND)
  find_package(OpenGL)
  if (OpenGL_FOUND)
    tests(opengl)
    foreach(test_name ${TEST_NAMES})
      target_link_libraries("${test_name}" PRIVATE "${OPENGL_LIBRARIES}")
    endforeach()
  endif()
endif()

if (WITH_TEST_GENERATOR)

  set(GEN_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generator")

  function(halide_define_aot_test NAME)
    set(options OMIT_DEFAULT_GENERATOR)
    set(oneValueArgs FUNCTION_NAME HALIDE_TARGET)
    set(multiValueArgs GENERATOR_ARGS DEPS FILTER_DEPS HALIDE_TARGET_FEATURES)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(TARGET "generator_aot_${NAME}")
    add_executable("${TARGET}" "${GEN_TEST_DIR}/${NAME}_aottest.cpp")
    target_include_directories("${TARGET}" PRIVATE
                               "${CMAKE_SOURCE_DIR}/tools"
                               "${CMAKE_SOURCE_DIR}"
                               "${CMAKE_SOURCE_DIR}/src/runtime")

    if (NOT ${args_OMIT_DEFAULT_GENERATOR})
      halide_library_from_generator("${NAME}"
                             GENERATOR "${NAME}.generator"
                             FUNCTION_NAME "${args_FUNCTION_NAME}"
                             HALIDE_TARGET "${args_HALIDE_TARGET}"
                             HALIDE_TARGET_FEATURES "${args_HALIDE_TARGET_FEATURES}"
                             GENERATOR_ARGS "${args_GENERATOR_ARGS}"
                             FILTER_DEPS "${args_FILTER_DEPS}")
      target_link_libraries("${TARGET}" PUBLIC "${NAME}")
    endif()

    add_test("${TARGET}"
             GROUPS test_generator run_tests)
  endfunction(halide_define_aot_test)

  # Find all the files of form "foo_generator.cpp" in test/generator
  # and declare a halide_generator() rule for them. Note that a few
  # have special-cases.
  file(GLOB GENERATOR_SRCS RELATIVE "${GEN_TEST_DIR}" "${GEN_TEST_DIR}/*_generator.cpp")
  foreach(FILE ${GENERATOR_SRCS})
    string(REPLACE "_generator.cpp" "" NAME "${FILE}")

    # Most generators have no extra deps...
    set(DEPS )
    # ...but a few do. We'll special-case them.
    if("${NAME}" STREQUAL "stubuser")
      set(DEPS "stubtest.generator" "configure.generator")
    elseif("${NAME}" STREQUAL "external_code")
      set(DEPS "external_code_generator_deps")
    endif()

    # Most of our Generators are one-per-file...
    set(NAMES "${NAME}")
    # ...but some have multiple-per-file
    if("${NAME}" STREQUAL "nested_externs")
      set(NAMES nested_externs_root nested_externs_inner nested_externs_combine nested_externs_leaf)
    endif()
    foreach(N ${NAMES})
      halide_generator("${N}.generator"
                       GENERATOR_NAME "${N}"
                       SRCS "${GEN_TEST_DIR}/${NAME}_generator.cpp"
                       DEPS ${DEPS})
    endforeach()
  endforeach()

  # Define a nontrivial depedency for external_code.generator
  set(EC32 "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/external_code_extern_bitcode_32")
  add_custom_command(OUTPUT "${EC32}.bc"
                     DEPENDS "${GEN_TEST_DIR}/external_code_extern.cpp"
                     COMMAND ${CLANG} ${CXX_WARNING_FLAGS} -O3 -c -m32 -target le32-unknown-nacl-unknown -emit-llvm "${GEN_TEST_DIR}/external_code_extern.cpp" -o "${EC32}.bc")
  add_custom_command(OUTPUT "${EC32}.cpp"
                     DEPENDS "${EC32}.bc"
                     COMMAND binary2cpp external_code_extern_bitcode_32 < "${EC32}.bc" > "${EC32}.cpp")

  set(EC64 "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/external_code_extern_bitcode_64")
  add_custom_command(OUTPUT "${EC64}.bc"
                     DEPENDS "${GEN_TEST_DIR}/external_code_extern.cpp"
                     COMMAND ${CLANG} ${CXX_WARNING_FLAGS} -O3 -c -m64 -target le64-unknown-unknown-unknown -emit-llvm "${GEN_TEST_DIR}/external_code_extern.cpp" -o "${EC64}.bc")
  add_custom_command(OUTPUT "${EC64}.cpp"
                     DEPENDS "${EC64}.bc"
                     COMMAND binary2cpp external_code_extern_bitcode_64 < "${EC64}.bc" > "${EC64}.cpp")

  set(ECCPP "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/external_code_extern_cpp_source")
  add_custom_command(OUTPUT "${ECCPP}.cpp"
                     DEPENDS "${GEN_TEST_DIR}/external_code_extern.cpp"
                     COMMAND binary2cpp external_code_extern_cpp_source < "${GEN_TEST_DIR}/external_code_extern.cpp" > "${ECCPP}.cpp")

  add_library(external_code_generator_deps "${EC32}.cpp" "${EC64}.cpp" "${ECCPP}.cpp")

  # ------ Generator tests for just-in-time mode: ------
  # Find all the files of form "foo_jittest.cpp" in test/generator
  # and declare a rule for them, each of which depends on foo.generator.
  file(GLOB JITTEST_SRCS RELATIVE "${GEN_TEST_DIR}" "${GEN_TEST_DIR}/*_jittest.cpp")
  foreach(FILE ${JITTEST_SRCS})
    string(REPLACE "_jittest.cpp" "" NAME "${FILE}")
    set(TARGET "generator_jit_${NAME}")
    halide_project("${TARGET}" "generator" "${GEN_TEST_DIR}/${FILE}")
    target_link_libraries("${TARGET}" PRIVATE "${NAME}.generator")
    add_test("${TARGET}"
             GROUPS test_generator run_tests)
  endforeach()

  # ------ Generator tests for ahead-of-time mode: ------
  # Find all the files of form "foo_aottest.cpp" in test/generator
  # and declare a rule for them, each of which depends on foo.generator.
  # Note that there are many special cases.
  # TODO(srj)
  # file(GLOB JITTEST_SRCS RELATIVE "${GEN_TEST_DIR}" "${GEN_TEST_DIR}/*_aottest.cpp")
  # foreach(FILE ${JITTEST_SRCS})
  #   string(REPLACE "_aottest.cpp" "" NAME "${FILE}")
  #   set(TARGET )

  #   add_executable("generator_aot_${NAME}" "${GEN_TEST_DIR}/${FILE}")
  #   target_include_directories("generator_aot_${NAME}" PRIVATE
  #                              "${CMAKE_SOURCE_DIR}"
  #                              "${CMAKE_SOURCE_DIR}/tools"
  #                              "${CMAKE_SOURCE_DIR}/src/runtime")

  #   if (NOT ${args_OMIT_DEFAULT_GENERATOR})
  #     halide_library_from_generator("${NAME}"
  #                            GENERATOR "${NAME}.generator"
  #                            FUNCTION_NAME "${args_FUNCTION_NAME}"
  #                            HALIDE_TARGET "${args_GENERATOR_HALIDE_TARGET}"
  #                            GENERATOR_ARGS "${args_GENERATOR_ARGS}"
  #                            FILTER_DEPS "${args_FILTER_DEPS}")
  #     target_link_libraries("generator_aot_${NAME}" PUBLIC "${NAME}")
  #   endif()
  # endforeach()

  # Create tests for ahead of-time-compiled generators. This will produce two
  # executables, one containing the generator itself
  # (e.g. from example_generator.cpp) and used at build time, and the other, the
  # test that executes the generated code (e.g. from example_aottest.cpp).

  # Tests with no special requirements
  halide_define_aot_test(acquire_release)
  halide_define_aot_test(argvcall)
  halide_define_aot_test(can_use_target)
  halide_define_aot_test(cleanup_on_error)
  halide_define_aot_test(configure)
  halide_define_aot_test(define_extern_opencl)
  halide_define_aot_test(embed_image)
  halide_define_aot_test(error_codes)
  halide_define_aot_test(example)
  halide_define_aot_test(float16_t)
  halide_define_aot_test(gpu_only)
  halide_define_aot_test(image_from_array)
  halide_define_aot_test(mandelbrot)
  halide_define_aot_test(stubuser)
  halide_define_aot_test(variable_num_threads)
  halide_define_aot_test(output_assign)
  halide_define_aot_test(external_code)

  # Tests that require nonstandard targets, namespaces, args, etc.
  halide_define_aot_test(matlab
                         HALIDE_TARGET_FEATURES matlab)

  halide_define_aot_test(memory_profiler_mandelbrot
                         HALIDE_TARGET_FEATURES profile)

  halide_define_aot_test(multitarget
                         HALIDE_TARGET host,host-debug
                         HALIDE_TARGET_FEATURES c_plus_plus_name_mangling
                         FUNCTION_NAME HalideTest::multitarget)

  halide_define_aot_test(user_context
                         HALIDE_TARGET_FEATURES user_context)

  halide_define_aot_test(user_context_insanity
                         HALIDE_TARGET_FEATURES user_context)

  add_library(cxx_mangling_externs
              "${GEN_TEST_DIR}/cxx_mangling_externs.cpp")

  halide_define_aot_test(cxx_mangling
                         HALIDE_TARGET_FEATURES c_plus_plus_name_mangling
                         FUNCTION_NAME HalideTest::AnotherNamespace::cxx_mangling
                         FILTER_DEPS cxx_mangling_externs)

  if (TARGET_PTX)
    halide_library_from_generator(cxx_mangling_gpu
                                  GENERATOR cxx_mangling.generator
                                  FUNCTION_NAME HalideTest::cxx_mangling_gpu
                                  HALIDE_TARGET_FEATURES c_plus_plus_name_mangling cuda)
    target_link_libraries(generator_aot_cxx_mangling PUBLIC cxx_mangling_gpu)
  endif()

  # gpu_object_lifetime can build with a variety of GPU targets (or none).
  # TODO: disable for now, as we currently run buildbot/Travis tests on machines
  # that don't have these available.
  # if(TARGET_PTX)
  #   halide_define_aot_test(gpu_object_lifetime
  #                          HALIDE_TARGET_FEATURES cuda debug)
  # elseif(TARGET_OPENCL)
  #   halide_define_aot_test(gpu_object_lifetime
  #                          HALIDE_TARGET_FEATURES opencl debug)
  # elseif(TARGET_METAL)
  #   halide_define_aot_test(gpu_object_lifetime
  #                          HALIDE_TARGET_FEATURES metal debug)
  # else()
    halide_define_aot_test(gpu_object_lifetime
                           HALIDE_TARGET_FEATURES debug)
  # endif()

  halide_define_aot_test(old_buffer_t
                         HALIDE_TARGET_FEATURES legacy_buffer_wrappers)

  halide_define_aot_test(pyramid
                         GENERATOR_ARGS levels=10)
  halide_define_aot_test(string_param
                         GENERATOR_ARGS rpn_expr="5 y * x +")

  halide_define_aot_test(msan
                         HALIDE_TARGET_FEATURES msan)

  # stubtest has input and output funcs with undefined types; this is fine for stub
  # usage (the types can be inferred), but for AOT compilation, we must make the types
  # concrete via generator args.
  set(STUBTEST_GENERATOR_ARGS
      untyped_buffer_input.type=uint8 untyped_buffer_input.dim=3
      simple_input.type=float32
      array_input.type=float32 array_input.size=2
      int_arg.size=2
      tuple_output.type=float32,float32
      vectorize=true
  )
  halide_define_aot_test(stubtest
                         GENERATOR_ARGS "${STUBTEST_GENERATOR_ARGS}")

  # Tests that require additional dependencies, args, etc
  set(MDTEST_GEN_ARGS
      input.type=uint8 input.dim=3
      dim_only_input_buffer.type=uint8
      untyped_input_buffer.type=uint8 untyped_input_buffer.dim=3
      output.type=float32,float32 output.dim=3
      input_not_nod.type=uint8 input_not_nod.dim=3
      input_nod.dim=3
      input_not.type=uint8
      array_input.size=2
      array_i8.size=2
      array_i16.size=2
      array_i32.size=2
      array_h.size=2
      buffer_array_input2.dim=3
      buffer_array_input3.type=float32
      buffer_array_input4.dim=3
      buffer_array_input4.type=float32
      buffer_array_input5.size=2
      buffer_array_input6.size=2
      buffer_array_input6.dim=3
      buffer_array_input7.size=2
      buffer_array_input7.type=float32
      buffer_array_input8.size=2
      buffer_array_input8.dim=3
      buffer_array_input8.type=float32
      buffer_f16_untyped.type=float16
      array_outputs.size=2
      array_outputs7.size=2
      array_outputs8.size=2
      array_outputs9.size=2
  )
  halide_define_aot_test(metadata_tester
                         GENERATOR_ARGS "${MDTEST_GEN_ARGS}")
  halide_library_from_generator(metadata_tester_ucon
                                GENERATOR metadata_tester.generator
                                HALIDE_TARGET_FEATURES user_context
                                GENERATOR_ARGS "${MDTEST_GEN_ARGS}"
                                EXTRA_OUTPUTS stmt assembly)
  target_link_libraries(generator_aot_metadata_tester PUBLIC metadata_tester_ucon)

  # Needs an extra library from this Generator
  halide_define_aot_test(alias)
  halide_library(alias_with_offset_42
                 SRCS ${GEN_TEST_DIR}/alias_generator.cpp
                 GENERATOR_NAME alias_with_offset_42)
  target_link_libraries(generator_aot_alias PUBLIC alias_with_offset_42)

  halide_define_aot_test(tiled_blur)
  halide_library_from_generator(blur2x2)
  target_link_libraries(generator_aot_tiled_blur PUBLIC blur2x2)

  add_library(cxx_mangling_define_extern_externs
              "${GEN_TEST_DIR}/cxx_mangling_define_extern_externs.cpp")
  target_link_libraries(cxx_mangling_define_extern_externs PUBLIC cxx_mangling_externs cxx_mangling)

  halide_define_aot_test(cxx_mangling_define_extern
                         HALIDE_TARGET_FEATURES c_plus_plus_name_mangling user_context
                         FUNCTION_NAME HalideTest::cxx_mangling_define_extern
                         FILTER_DEPS cxx_mangling_externs cxx_mangling_define_extern_externs)
  # The cxx_mangling library was already defined implicitly, above,
  # so just add a dependency on it
  target_link_libraries(generator_aot_cxx_mangling_define_extern PUBLIC cxx_mangling)

  halide_define_aot_test(nested_externs OMIT_DEFAULT_GENERATOR)
  foreach(G root inner combine leaf)
    halide_library_from_generator(nested_externs_${G})
    target_link_libraries(generator_aot_nested_externs PRIVATE nested_externs_${G})
  endforeach()

endif()
