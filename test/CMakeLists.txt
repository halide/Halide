option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
option(WITH_TEST_ERROR "Build error tests" ON)
option(WITH_TEST_WARNING "Build warning tests" ON)
option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
option(WITH_TEST_OPENGL "Build OpenGL tests" ON)
option(WITH_TEST_GENERATORS "Build Generator tests" ON)

if (MSVC)
else()
  # src/CMakeLists.txt always enables C++11 for non-Windows,
  # so we'll give the same courtesy to tests
  add_definitions("-std=c++11")
  add_definitions("-fno-rtti")
endif()

function(tests folder)
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${folder}" "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp")
  if (WIN32)
    LIST(REMOVE_ITEM TESTS "simd_op_check.cpp") # Relies on shell stuff that doesn't work on windows
  endif()
  foreach(file ${TESTS})
    string(REPLACE ".cpp" "" name "${file}")
    halide_project("${folder}_${name}" "${folder}" "${folder}/${file}")
  endforeach()
endfunction(tests)

include_directories ("${CMAKE_BINARY_DIR}/include")
link_directories ("${LLVM_LIB}")

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
  tests(opengl)
endif()

if (WITH_TEST_GENERATORS)

  # Generator tests for just-in-time mode:
  # For foo_generator.cpp, assume it contains a single generator named 'foo'
  # and make an executable target for it named 'generator_foo'
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/generator" "${CMAKE_CURRENT_SOURCE_DIR}/generator/*_jittest.cpp")
  foreach(test ${TESTS})
    string(REPLACE "_jittest.cpp" "" name "generator_jit_${test}")
    halide_project(${name} "generator" "generator/${test}")
  endforeach()

  # Generator tests for ahead-of-time mode:

  # Create tests for ahead of-time-compiled generators. This will produce two
  # executables, one containing the generator itself
  # (e.g. from example_generator.cpp) and used at build time, and the other, the
  # test that executes the generated code (e.g. from example_aottest.cpp).

  # For foo_generator.cpp, assume it contains a single generator named 'foo'
  # and make an executable target for it named 'generator_foo' that contains the
  # generator executable.

  include(../HalideGenerator.cmake)

  # First create Halide linked executables for the generators using
  # halide_project(...). These are used during the build.
  file(GLOB GENS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/generator" "${CMAKE_CURRENT_SOURCE_DIR}/generator/*_generator.cpp")
  foreach(gen_src ${GENS})
    string(REPLACE "_generator.cpp" "" gen_name "generator_${gen_src}")
    # tiled_blur also needs tiled_blur_blur
    if(gen_name STREQUAL "generator_tiled_blur")
      halide_project(${gen_name} "generator" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/GenGen.cpp" "generator/${gen_src}" "generator/tiled_blur_blur_generator.cpp")
    else()
      halide_project(${gen_name} "generator" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/GenGen.cpp" "generator/${gen_src}")
    endif()
  endforeach()

  # Next, invoke each of the generator executables with the arguments specific
  # to each test case.
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/generator" "${CMAKE_CURRENT_SOURCE_DIR}/generator/*_aottest.cpp")
  foreach(TEST_SRC ${TESTS})

    string(REPLACE "_aottest.cpp" "" GEN_NAME "${TEST_SRC}")
    string(REPLACE "_aottest.cpp" "" FUNC_NAME "${TEST_SRC}")

    string(REPLACE ".cpp" "" TEST_RUNNER "exec_test_${TEST_SRC}")

    file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/" NATIVE_INT_DIR)
    set(SCRATCH_DIR "${NATIVE_INT_DIR}scratch_${GEN_NAME}")
    file(MAKE_DIRECTORY "${SCRATCH_DIR}")

    add_executable("${TEST_RUNNER}" "generator/${TEST_SRC}")

    # Add special case dependencies certain files that globbed above.
    if (TEST_SRC STREQUAL "tiled_blur_interleaved_aottest.cpp")
      # Some special dependencies here:
      # "tiled_blur_interleaved" is produced by using tiled_blur with different generator args.
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_tiled_blur"
                               "tiled_blur"
                               "tiled_blur_interleaved"
                               "target=host" "is_interleaved=true")
      # "tiled_blur_blur_interleaved" is produced by using tiled_blur_blur with different generator args.
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_tiled_blur_blur"
                               "tiled_blur_blur"
                               "tiled_blur_blur_interleaved"
                               "target=host" "is_interleaved=true")
    elseif(TEST_SRC STREQUAL "nested_externs_aottest.cpp")

      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_nested_externs"
                               "nested_externs_root"
                               "nested_externs_root"
                               "target=host")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_nested_externs"
                               "nested_externs_inner"
                               "nested_externs_inner"
                               "target=host")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_nested_externs"
                               "nested_externs_combine"
                               "nested_externs_combine"
                               "target=host")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_nested_externs"
                               "nested_externs_leaf"
                               "nested_externs_leaf"
                               "target=host")
    elseif(TEST_SRC STREQUAL "user_context_aottest.cpp")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host-user_context")
    elseif(TEST_SRC STREQUAL "user_context_insanity_aottest.cpp")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host-user_context")
    # metadata_tester_aottest.cpp depends on two variants of metadata_generator
    elseif(TEST_SRC STREQUAL "metadata_tester_aottest.cpp")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host-register_metadata")
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}_ucon"
                               "target=host-register_metadata-user_context")
    else()
      # All the other foo_test.cpp just depend on foo_generator.cpp
      halide_add_generator_dependency("${TEST_RUNNER}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host")
      # tiled_blur_aottest.cpp depends on tiled_blur AND ALSO tiled_blur_blur
      if (TEST_SRC STREQUAL "tiled_blur_aottest.cpp")
        halide_add_generator_dependency("${TEST_RUNNER}"
                                 "generator_tiled_blur_blur"
                                 "tiled_blur_blur"
                                 "tiled_blur_blur"
                                 "target=host")
      endif()
    endif()
    target_include_directories("${TEST_RUNNER}" PRIVATE "${CMAKE_SOURCE_DIR}/apps/support")
    target_include_directories("${TEST_RUNNER}" PRIVATE "${CMAKE_SOURCE_DIR}/src/runtime")
    set_target_properties("${TEST_RUNNER}" PROPERTIES FOLDER "generator")
    if (WIN32)
      # /FORCE:multiple allows clobbering the halide runtime symbols in the lib
      set_target_properties("${TEST_RUNNER}" PROPERTIES LINK_FLAGS "/STACK:8388608,1048576 /FORCE:multiple")
    else()
      target_link_libraries("${TEST_RUNNER}" dl pthread z)
      set_target_properties("${TEST_RUNNER}" PROPERTIES LINK_FLAGS "-L ${SCRATCH_DIR}")
    endif()
  endforeach()
endif()
