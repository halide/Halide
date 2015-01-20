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

function(add_generator_dependency target scratch_dir gen_target gen_name func_name)
  # CMake 2.8 doesn't have string(CONCAT), so fake it like so:
  string(REPLACE ".lib" "${CMAKE_STATIC_LIBRARY_SUFFIX}" FILTER_LIB "${func_name}.lib" )
  string(REPLACE ".h" ".h" FILTER_HDR "${func_name}.h" )

  if (WIN32)
    # TODO(srj): this has not yet been tested on Windows.
    add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
      DEPENDS "${gen_target}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/${BUILD_TYPE}/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${scratch_dir}" ${ARGN}
      COMMAND "lib.exe" "/OUT:${FILTER_LIB}" "${scratch_dir}\\${func_name}.o"
      WORKING_DIRECTORY "${scratch_dir}"
      )
  else()
    add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
      DEPENDS "${gen_target}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${scratch_dir}" ${ARGN}
      COMMAND "${CMAKE_AR}" q "${FILTER_LIB}" "${scratch_dir}/${func_name}.o"
      WORKING_DIRECTORY "${scratch_dir}")
  endif()

  # Use a custom target to force it to run the generator before the
  # object file for the runner.
  add_custom_target("exec_generator_${gen_name}_${func_name}" DEPENDS "${FILTER_LIB}" "${FILTER_HDR}")
  set_target_properties("exec_generator_${gen_name}_${func_name}" PROPERTIES FOLDER "generator")

  # exec_generator_foo must build before $target
  add_dependencies("${target}" "exec_generator_${gen_name}_${func_name}")

  target_link_libraries("${target}" "${scratch_dir}/${FILTER_LIB}")

  # be sure that $scratch_dir is in $target's include path
  target_include_directories("${target}" PRIVATE "${scratch_dir}")
endfunction(add_generator_dependency)


if (WITH_TEST_GENERATORS)
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/" NATIVE_INT_DIR)

  # For foo_generator.cpp, assume it contains a single generator named 'foo'
  # and make an executable target for it named 'generator_foo'
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/generator" "${CMAKE_CURRENT_SOURCE_DIR}/generator/*_jittest.cpp")
  foreach(test ${TESTS})
    string(REPLACE "_jittest.cpp" "" name "generator_jit_${test}")
    halide_project(${name} "generator" "generator/${test}")
  endforeach()

  # For foo_generator.cpp, assume it contains a single generator named 'foo'
  # and make an executable target for it named 'generator_foo'
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

  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/generator" "${CMAKE_CURRENT_SOURCE_DIR}/generator/*_aottest.cpp")
  foreach(TEST_SRC ${TESTS})

    string(REPLACE "_aottest.cpp" "" GEN_NAME "${TEST_SRC}")
    string(REPLACE "_aottest.cpp" "" FUNC_NAME "${TEST_SRC}")

    string(REPLACE ".cpp" "" TEST_RUNNER "exec_test_${TEST_SRC}")
    set(SCRATCH_DIR "${NATIVE_INT_DIR}scratch_${GEN_NAME}")
    file(MAKE_DIRECTORY "${SCRATCH_DIR}")

    add_executable("${TEST_RUNNER}" "generator/${TEST_SRC}")
    if (TEST_SRC STREQUAL "tiled_blur_interleaved_aottest.cpp")
      # Some special dependencies here:
      # "tiled_blur_interleaved" is produced by using tiled_blur with different generator args.
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_tiled_blur"
                               "tiled_blur"
                               "tiled_blur_interleaved"
                               "target=host" "is_interleaved=true")
      # "tiled_blur_blur_interleaved" is produced by using tiled_blur_blur with different generator args.
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_tiled_blur_blur"
                               "tiled_blur_blur"
                               "tiled_blur_blur_interleaved"
                               "target=host" "is_interleaved=true")
    elseif(TEST_SRC STREQUAL "nested_externs_aottest.cpp")
      # Dear CMake: I hate you, and I will hate you for all eternity.
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_nested_externs"
                               "nested_externs_root"
                               "nested_externs_root"
                               "target=host")
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_nested_externs"
                               "nested_externs_inner"
                               "nested_externs_inner"
                               "target=host")
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_nested_externs"
                               "nested_externs_combine"
                               "nested_externs_combine"
                               "target=host")
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_nested_externs"
                               "nested_externs_leaf"
                               "nested_externs_leaf"
                               "target=host")
    elseif(TEST_SRC STREQUAL "user_context_aottest.cpp")
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host-user_context")
    elseif(TEST_SRC STREQUAL "user_context_insanity_aottest.cpp")
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host-user_context")
    else()
      # All the other foo_test.cpp just depend on foo_generator.cpp
      add_generator_dependency("${TEST_RUNNER}"
                               "${SCRATCH_DIR}"
                               "generator_${GEN_NAME}"
                               "${GEN_NAME}"
                               "${FUNC_NAME}"
                               "target=host")
      # tiled_blur_aottest.cpp depends on tiled_blur AND ALSO tiled_blur_blur
      if (TEST_SRC STREQUAL "tiled_blur_aottest.cpp")
        add_generator_dependency("${TEST_RUNNER}"
                                 "${SCRATCH_DIR}"
                                 "generator_tiled_blur_blur"
                                 "tiled_blur_blur"
                                 "tiled_blur_blur"
                                 "target=host")
      endif()
    endif()
    target_include_directories("${TEST_RUNNER}" PRIVATE "${CMAKE_SOURCE_DIR}/apps/support")
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
