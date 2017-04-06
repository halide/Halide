option(WITH_TEST_INTERNAL "Build and run internal tests" ON)
option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
option(WITH_TEST_ERROR "Build error tests" ON)
option(WITH_TEST_WARNING "Build warning tests" ON)
option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
option(WITH_TEST_OPENGL "Build OpenGL tests" ON)
option(WITH_TEST_GENERATORS "Build Generator tests" ON)

if (WITH_TEST_INTERNAL)
  message(STATUS "Internal tests enabled")
  halide_project(test_internal internal internal.cpp)
  # Run internal tests after building
  add_custom_command(TARGET test_internal
                     POST_BUILD
                     COMMAND test_internal
                     COMMENT "Running internal tests")
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
    # Test links against libHalide
    halide_project("${folder}_${name}" "${folder}" "${folder}/${file}")
    list(APPEND TEST_NAMES "${folder}_${name}")
    target_include_directories("${folder}_${name}" PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_definitions("${folder}_${name}"  PRIVATE "-DLLVM_VERSION=${LLVM_VERSION}")
  endforeach()
  set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction(tests)

if (WITH_TEST_CORRECTNESS)
  tests(correctness)
  use_image_io(correctness_image_io)
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
  find_package(OpenGL)
  if (OpenGL_FOUND)
    tests(opengl)
    foreach(test_name ${TEST_NAMES})
      target_link_libraries("${test_name}" PRIVATE "${OPENGL_LIBRARIES}")
    endforeach()
  endif()
endif()

function(add_test_generator NAME)
  set(options WITH_STUB)
  set(oneValueArgs GENERATOR_NAME)
  set(multiValueArgs STUB_DEPS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  # convert TRUE|FALSE -> WITH_STUB|emptystring
  if (${args_WITH_STUB})
    set(args_WITH_STUB WITH_STUB)
  else()
    set(args_WITH_STUB )
  endif()
  halide_add_generator("${NAME}.generator"
                       STUB_GENERATOR_NAME "${args_GENERATOR_NAME}"
                       SRCS "${CMAKE_CURRENT_SOURCE_DIR}/generator/${NAME}_generator.cpp"
                       STUB_DEPS ${args_STUB_DEPS}
                       ${args_WITH_STUB})
endfunction(add_test_generator)

function(halide_define_jit_test NAME)
  set(options )
  set(oneValueArgs )
  set(multiValueArgs STUB_DEPS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(ALLSTUBS )
  foreach(STUB ${args_STUB_DEPS})
    list(APPEND ALLSTUBS $<TARGET_OBJECTS:${STUB}.objlib>)
  endforeach()

  halide_project("generator_jit_${NAME}"
                 "generator"
                 "${CMAKE_CURRENT_SOURCE_DIR}/generator/${NAME}_jittest.cpp"
                 ${ALLSTUBS})
  foreach(STUB_DEP ${args_STUB_DEPS})
    halide_add_generator_stub_dependency(TARGET "generator_jit_${NAME}"
                                         STUB_GENERATOR_TARGET ${STUB_DEP})
  endforeach()
endfunction(halide_define_jit_test)

include(../HalideGenerator.cmake)

# Usage:
#   halide_define_aot_test(NAME <basename>
#                       [GENERATED_FUNCTION <function name, including namespace]
#                       [GENERATOR_HALIDE_TARGET Halide target, if not "host"]
#                       [GENERATOR_ARGS extra generator args])
function(halide_define_aot_test NAME)
  set(options OMIT_DEFAULT_GENERATOR)
  set(oneValueArgs GENERATED_FUNCTION GENERATOR_NAME GENERATOR_HALIDE_TARGET)
  set(multiValueArgs GENERATOR_ARGS STUB_DEPS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(TARGET "generator_aot_${NAME}")

  add_executable("${TARGET}" "${CMAKE_CURRENT_SOURCE_DIR}/generator/${NAME}_aottest.cpp")
  if (MSVC)
  else()
    target_compile_options("${TARGET}" PUBLIC "-std=c++11") # HalideBuffer.h needs C++11
  endif()
  target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}/tools")
  target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}/src/runtime")
  target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}")
  set_target_properties("${TARGET}" PROPERTIES FOLDER "generator")
  foreach(STUB_DEP ${args_STUB_DEPS})
    halide_add_generator_stub_dependency(TARGET "${TARGET}"
                                         STUB_GENERATOR_TARGET ${STUB_DEP})
  endforeach()
  if (MSVC)
    # 4088: "/FORCE used, image may not work"
    set_target_properties("${TARGET}" PROPERTIES LINK_FLAGS "/ignore:4088")
  endif()

  if (NOT ${args_OMIT_DEFAULT_GENERATOR})
    if (NOT args_GENERATED_FUNCTION)
      set(args_GENERATED_FUNCTION "${NAME}")
    endif()
    if (NOT args_GENERATOR_HALIDE_TARGET)
      set(args_GENERATOR_HALIDE_TARGET "host")
    endif()
    if (NOT args_GENERATOR_NAME)
      set(args_GENERATOR_NAME ${NAME})
    endif()

    set(AOT_LIBRARY_TARGET "${NAME}")
    halide_add_aot_library("${AOT_LIBRARY_TARGET}"
                           GENERATOR_TARGET "${NAME}.generator"
                           GENERATOR_NAME "${args_GENERATOR_NAME}"
                           GENERATED_FUNCTION "${args_GENERATED_FUNCTION}"
                           GENERATOR_ARGS "target=${args_GENERATOR_HALIDE_TARGET}" "${args_GENERATOR_ARGS}")
    halide_add_aot_library_dependency("${TARGET}" "${AOT_LIBRARY_TARGET}")
  endif()

endfunction(halide_define_aot_test)

function(halide_add_aot_test_dependency NAME)
  set(options )
  set(oneValueArgs AOT_LIBRARY_TARGET GENERATED_FUNCTION GENERATOR_NAME GENERATOR_TARGET GENERATOR_HALIDE_TARGET)
  set(multiValueArgs GENERATOR_ARGS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(TARGET "generator_aot_${NAME}")

  if (NOT args_GENERATOR_TARGET)
    set(args_GENERATOR_TARGET "${NAME}")
  endif()
  if (NOT args_GENERATED_FUNCTION)
    set(args_GENERATED_FUNCTION "${args_AOT_LIBRARY_TARGET}")
  endif()
    if (NOT args_GENERATOR_HALIDE_TARGET)
      set(args_GENERATOR_HALIDE_TARGET "host")
    endif()

  halide_add_aot_library("${args_AOT_LIBRARY_TARGET}"
                         GENERATOR_TARGET "${args_GENERATOR_TARGET}.generator"
                         GENERATOR_NAME "${args_GENERATOR_NAME}"
                         GENERATED_FUNCTION "${args_GENERATED_FUNCTION}"
                         GENERATOR_ARGS "target=${args_GENERATOR_HALIDE_TARGET}" "${args_GENERATOR_ARGS}")
  halide_add_aot_library_dependency("${TARGET}" "${args_AOT_LIBRARY_TARGET}")
endfunction(halide_add_aot_test_dependency)

if (WITH_TEST_GENERATORS)

  # Explicitly define the generator targets; each has a build target
  # of the form "name.generator"
  add_test_generator(acquire_release)
  add_test_generator(argvcall)
  add_test_generator(can_use_target)
  add_test_generator(cleanup_on_error)
  add_test_generator(cxx_mangling_define_extern)
  add_test_generator(cxx_mangling)
  add_test_generator(embed_image)
  add_test_generator(error_codes)
  add_test_generator(example WITH_STUB)
  add_test_generator(float16_t)
  add_test_generator(gpu_object_lifetime)
  add_test_generator(gpu_only)
  add_test_generator(image_from_array)
  add_test_generator(mandelbrot)
  add_test_generator(matlab)
  add_test_generator(memory_profiler_mandelbrot)
  add_test_generator(metadata_tester)
  add_test_generator(msan)
  add_test_generator(multitarget)
  add_test_generator(nested_externs)
  add_test_generator(pyramid)
  add_test_generator(stubtest WITH_STUB
                     GENERATOR_NAME StubNS1::StubNS2::StubTest)
  # the stubuser generator needs the stub from stubtest
  add_test_generator(stubuser
                     GENERATOR_NAME stubuser
                     STUB_DEPS stubtest.generator)
  add_test_generator(blur2x2)
  add_test_generator(tiled_blur)
  add_test_generator(user_context)
  add_test_generator(user_context_insanity)
  add_test_generator(variable_num_threads)

  # ------ Generator tests for just-in-time mode: ------
  halide_define_jit_test(example
                         STUB_DEPS example.generator)
  halide_define_jit_test(stubtest
                         STUB_DEPS stubtest.generator)

  # ------ Generator tests for ahead-of-time mode: ------

  # Create tests for ahead of-time-compiled generators. This will produce two
  # executables, one containing the generator itself
  # (e.g. from example_generator.cpp) and used at build time, and the other, the
  # test that executes the generated code (e.g. from example_aottest.cpp).

  # Tests with no special requirements
  halide_define_aot_test(acquire_release)
  halide_define_aot_test(argvcall)
  halide_define_aot_test(can_use_target)
  halide_define_aot_test(cleanup_on_error)
  halide_define_aot_test(embed_image)
  halide_define_aot_test(error_codes)
  halide_define_aot_test(example)
  halide_define_aot_test(float16_t)
  halide_define_aot_test(gpu_object_lifetime)
  halide_define_aot_test(gpu_only)
  halide_define_aot_test(image_from_array)
  halide_define_aot_test(mandelbrot)
  halide_define_aot_test(memory_profiler_mandelbrot)
  halide_define_aot_test(stubuser)
  halide_define_aot_test(variable_num_threads)

  # Tests that require nonstandard targets, namespaces, args, etc.
  halide_define_aot_test(matlab
                         GENERATOR_HALIDE_TARGET host-matlab)

  halide_define_aot_test(multitarget
                         GENERATOR_HALIDE_TARGET host-debug-c_plus_plus_name_mangling,host-c_plus_plus_name_mangling
                         GENERATED_FUNCTION HalideTest::multitarget)

  halide_define_aot_test(user_context
                         GENERATOR_HALIDE_TARGET host-user_context)

  halide_define_aot_test(user_context_insanity
                         GENERATOR_HALIDE_TARGET host-user_context)

  halide_define_aot_test(cxx_mangling
                         GENERATOR_HALIDE_TARGET host-c_plus_plus_name_mangling
                         GENERATED_FUNCTION HalideTest::cxx_mangling)
  if (TARGET_PTX)
    halide_add_aot_test_dependency(cxx_mangling
                                 AOT_LIBRARY_TARGET cxx_mangling_gpu
                                 GENERATED_FUNCTION HalideTest::cxx_mangling_gpu
                                 GENERATOR_HALIDE_TARGET host-c_plus_plus_name_mangling-cuda)
  endif()

  halide_define_aot_test(pyramid
                         GENERATOR_ARGS levels=10)

  halide_define_aot_test(msan
                         GENERATOR_HALIDE_TARGET host-msan)

  # stubtest has input and output funcs with undefined types; this is fine for stub
  # usage (the types can be inferred), but for AOT compilation, we must make the types
  # concrete via generator args.
  #
  # Also note that setting 'vectorize=true' is redundant (that's the default), but verifies
  # that setting ScheduleParam via generator_args works properly.
  set(STUBTEST_GENERATOR_ARGS
      untyped_buffer_input.type=uint8 untyped_buffer_input.dim=3
      simple_input.type=float32
      array_input.type=float32 array_input.size=2
      int_arg.size=2
      tuple_output.type=float32,float32
      vectorize=true
  )
  halide_define_aot_test(stubtest
                         GENERATOR_NAME StubNS1::StubNS2::StubTest
                         GENERATOR_ARGS "${STUBTEST_GENERATOR_ARGS}")

  # Tests that require additional dependencies, args, etc
  set(MDTEST_GEN_ARGS
      input.type=uint8 input.dim=3
      type_only_input_buffer.dim=3
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
      array_outputs.size=2
  )
  halide_define_aot_test(metadata_tester
                         GENERATOR_ARGS "${MDTEST_GEN_ARGS}")
  halide_add_aot_test_dependency(metadata_tester
                                 AOT_LIBRARY_TARGET metadata_tester_ucon
                                 GENERATED_FUNCTION metadata_tester_ucon
                                 GENERATOR_HALIDE_TARGET host-user_context
                                 GENERATOR_ARGS ${MDTEST_GEN_ARGS})

  halide_define_aot_test(tiled_blur)
  halide_add_aot_test_dependency(tiled_blur
                                 GENERATOR_TARGET blur2x2
                                 AOT_LIBRARY_TARGET blur2x2)

  halide_define_aot_test(cxx_mangling_define_extern
                         GENERATOR_HALIDE_TARGET host-c_plus_plus_name_mangling-user_context
                         GENERATED_FUNCTION HalideTest::cxx_mangling_define_extern)
  # The cxx_mangling library was already defined implicitly, above,
  # so just add a dependency on it
  halide_add_aot_library_dependency(generator_aot_cxx_mangling_define_extern cxx_mangling)

  halide_define_aot_test(nested_externs OMIT_DEFAULT_GENERATOR)
  halide_add_aot_test_dependency(nested_externs
                                 AOT_LIBRARY_TARGET nested_externs_root
                                 GENERATOR_NAME nested_externs_root)
  halide_add_aot_test_dependency(nested_externs
                                 AOT_LIBRARY_TARGET nested_externs_inner
                                 GENERATOR_NAME nested_externs_inner)
  halide_add_aot_test_dependency(nested_externs
                                 AOT_LIBRARY_TARGET nested_externs_combine
                                 GENERATOR_NAME nested_externs_combine)
  halide_add_aot_test_dependency(nested_externs
                                 AOT_LIBRARY_TARGET nested_externs_leaf
                                 GENERATOR_NAME nested_externs_leaf)

endif()
