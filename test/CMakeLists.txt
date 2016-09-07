option(WITH_TEST_INTERNAL "Build and run internal tests" ON)
option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
option(WITH_TEST_ERROR "Build error tests" ON)
option(WITH_TEST_WARNING "Build warning tests" ON)
option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
option(WITH_TEST_OPENGL "Build OpenGL tests" ON)
option(WITH_TEST_GENERATORS "Build Generator tests" ON)
option(WITH_TEST_RENDERSCRIPT "Build Renderscript tests" ON)

if (MSVC)
else()
  # src/CMakeLists.txt always enables C++11 for non-Windows,
  # so we'll give the same courtesy to tests
  add_definitions("-std=c++11")
  add_definitions("-fno-rtti")
endif()

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

function(tests folder)
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${folder}" "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp")
  if (WIN32)
    LIST(REMOVE_ITEM TESTS "simd_op_check.cpp") # Relies on shell stuff that doesn't work on windows
  endif()
  set(TEST_NAMES "")
  foreach(file ${TESTS})
    string(REPLACE ".cpp" "" name "${file}")
    # Test links against libHalide
    halide_project("${folder}_${name}" "${folder}" "${folder}/${file}")
    list(APPEND TEST_NAMES "${folder}_${name}")
  endforeach()
  set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction(tests)

if (WITH_TEST_CORRECTNESS)
  tests(correctness)
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
if (WITH_TEST_RENDERSCRIPT)
  tests(renderscript)
endif()

function(halide_define_jit_test NAME)
  set(options )
  set(oneValueArgs )
  set(multiValueArgs )
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  halide_project("generator_jit_${NAME}" 
                 "generator" 
                 "${CMAKE_CURRENT_SOURCE_DIR}/generator/${NAME}_jittest.cpp")
endfunction(halide_define_jit_test)

function(halide_add_generator NAME)
  set(options )
  set(oneValueArgs )
  set(multiValueArgs )
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  halide_project("${NAME}.generator" 
                 "generator" 
                 "${CMAKE_SOURCE_DIR}/tools/GenGen.cpp" 
                 "${CMAKE_CURRENT_SOURCE_DIR}/generator/${NAME}_generator.cpp")
endfunction(halide_add_generator)

include(../HalideGenerator.cmake)

# Usage:
#   halide_define_aot_test(NAME <basename>
#                       [GENERATED_FUNCTION <function name, including namespace]
#                       [GENERATOR_HALIDE_TARGET Halide target, if not "host"]
#                       [GENERATOR_ARGS extra generator args])
function(halide_define_aot_test NAME)
  set(options OMIT_DEFAULT_GENERATOR)
  set(oneValueArgs GENERATED_FUNCTION GENERATOR_HALIDE_TARGET)
  set(multiValueArgs GENERATOR_ARGS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(TARGET "generator_aot_${NAME}")

  add_executable("${TARGET}" "${CMAKE_CURRENT_SOURCE_DIR}/generator/${NAME}_aottest.cpp")
  target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}/tools")
  target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}/src/runtime")
  set_target_properties("${TARGET}" PROPERTIES FOLDER "generator")

  if (NOT ${args_OMIT_DEFAULT_GENERATOR})
    if (NOT args_GENERATED_FUNCTION)
      set(args_GENERATED_FUNCTION "${NAME}")
    endif()
    if (NOT args_GENERATOR_HALIDE_TARGET)
      set(args_GENERATOR_HALIDE_TARGET "host")
    endif()

    set(AOT_LIBRARY_TARGET "${NAME}")
    halide_add_aot_library("${AOT_LIBRARY_TARGET}"
                           GENERATOR_TARGET "${NAME}.generator"
                           GENERATOR_NAME "${NAME}"
                           GENERATED_FUNCTION "${args_GENERATED_FUNCTION}"
                           GENERATOR_ARGS "target=${args_GENERATOR_HALIDE_TARGET}" "${GENERATOR_ARGS}")
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
  if (NOT args_GENERATOR_NAME)
    set(args_GENERATOR_NAME "${args_GENERATOR_TARGET}")
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

  # ------ Generator tests for just-in-time mode: ------
  halide_define_jit_test(example)
  halide_define_jit_test(paramtest)

  # ------ Generator tests for ahead-of-time mode: ------

  # Create tests for ahead of-time-compiled generators. This will produce two
  # executables, one containing the generator itself
  # (e.g. from example_generator.cpp) and used at build time, and the other, the
  # test that executes the generated code (e.g. from example_aottest.cpp).

  # Explicitly define the generator targets; each has a build target
  # of the form "name.generator"
  halide_add_generator(acquire_release)
  halide_add_generator(argvcall)
  halide_add_generator(can_use_target)
  halide_add_generator(cleanup_on_error)
  halide_add_generator(cxx_mangling_define_extern)
  halide_add_generator(cxx_mangling)
  halide_add_generator(embed_image)
  halide_add_generator(error_codes)
  halide_add_generator(example)
  halide_add_generator(extended_buffer_t)
  halide_add_generator(float16_t)
  halide_add_generator(gpu_object_lifetime)
  halide_add_generator(gpu_only)
  halide_add_generator(image_from_array)
  halide_add_generator(mandelbrot)
  halide_add_generator(matlab)
  halide_add_generator(memory_profiler_mandelbrot)
  halide_add_generator(metadata_tester)
  halide_add_generator(multitarget)
  halide_add_generator(nested_externs)
  halide_add_generator(paramtest)
  halide_add_generator(pyramid)
  halide_add_generator(tiled_blur_blur)
  halide_add_generator(tiled_blur)
  halide_add_generator(user_context)
  halide_add_generator(user_context_insanity)
  halide_add_generator(variable_num_threads)

  # Tests with no special requirements
  halide_define_aot_test(acquire_release)
  halide_define_aot_test(argvcall)
  halide_define_aot_test(can_use_target)
  halide_define_aot_test(cleanup_on_error)
  halide_define_aot_test(embed_image)
  halide_define_aot_test(error_codes)
  halide_define_aot_test(example)
  halide_define_aot_test(extended_buffer_t)
  halide_define_aot_test(float16_t)
  halide_define_aot_test(gpu_object_lifetime)
  halide_define_aot_test(gpu_only)
  halide_define_aot_test(image_from_array)
  halide_define_aot_test(mandelbrot)
  halide_define_aot_test(matlab)
  halide_define_aot_test(memory_profiler_mandelbrot)
  halide_define_aot_test(pyramid)
  halide_define_aot_test(variable_num_threads)

  # Tests that require nonstandard targets, namespaces, etc.
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

  # Tests that require additional dependencies
  halide_define_aot_test(metadata_tester)
  halide_add_aot_test_dependency(metadata_tester
                                 AOT_LIBRARY_TARGET metadata_tester_ucon
                                 GENERATED_FUNCTION metadata_tester_ucon
                                 GENERATOR_HALIDE_TARGET host-user_context)

  halide_define_aot_test(tiled_blur)
  halide_add_aot_test_dependency(tiled_blur
                                 GENERATOR_TARGET tiled_blur_blur
                                 AOT_LIBRARY_TARGET tiled_blur_blur)

  halide_define_aot_test(tiled_blur_interleaved OMIT_DEFAULT_GENERATOR)
  halide_add_aot_test_dependency(tiled_blur_interleaved
                                 GENERATOR_TARGET tiled_blur
                                 AOT_LIBRARY_TARGET tiled_blur_interleaved
                                 GENERATOR_ARGS is_interleaved=true)
  halide_add_aot_test_dependency(tiled_blur_interleaved
                                 GENERATOR_TARGET tiled_blur_blur
                                 AOT_LIBRARY_TARGET tiled_blur_blur_interleaved
                                 GENERATOR_ARGS is_interleaved=true)

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
