option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
option(WITH_TEST_ERROR "Build error tests" ON)
option(WITH_TEST_WARNING "Build warning tests" ON)
option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
option(WITH_TEST_STATIC "Build static tests" ON)

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

if (WITH_TEST_STATIC)
  # The generators for the static tests
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/static" "${CMAKE_CURRENT_SOURCE_DIR}/static/*_generate.cpp")
  foreach(file ${TESTS})
    string(REPLACE "_generate.cpp" "" name "generate_${file}")
    halide_project("${name}" "static" "static/${file}")
  endforeach()

  file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/" NATIVE_INT_DIR)

  # The actual static tests
  file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/static" "${CMAKE_CURRENT_SOURCE_DIR}/static/*_test.cpp")
  foreach(file ${TESTS})

    # Make the object files as a static lib
    string(REPLACE "_test.cpp" "${CMAKE_STATIC_LIBRARY_SUFFIX}" STATIC_LIB "lib${file}")
    string(REPLACE "_test.cpp" "" GENERATOR "generate_${file}")
    string(REPLACE ".cpp" "" RUNNER "static_${file}")
    set(SCRATCH_DIR "${NATIVE_INT_DIR}scratch_${GENERATOR}")
    file(MAKE_DIRECTORY "${SCRATCH_DIR}")

    add_custom_command(OUTPUT "${STATIC_LIB}"
      DEPENDS "${GENERATOR}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/${GENERATOR}"
      COMMAND "${CMAKE_AR}" q "${STATIC_LIB}" "${SCRATCH_DIR}/*.o"
      WORKING_DIRECTORY "${SCRATCH_DIR}"
      )

    # Use a custom target to force it to run the generator before the
    # object file for the runner.
    add_custom_target("run_generator_${GENERATOR}" DEPENDS "${STATIC_LIB}")

    # Link them into the static tests
    add_executable("${RUNNER}" "static/${file}")
    add_dependencies("${RUNNER}" "run_generator_${GENERATOR}")
    target_include_directories("${RUNNER}" PRIVATE "${SCRATCH_DIR}")
    target_include_directories("${RUNNER}" PRIVATE "${CMAKE_SOURCE_DIR}/apps/support")
    target_link_libraries("${RUNNER}" "${STATIC_LIB}")
    set_target_properties("${RUNNER}" PROPERTIES FOLDER "static")
    if (WIN32)
      set_target_properties("${RUNNER}" PROPERTIES LINK_FLAGS "/STACK:8388608,1048576")
    else()
      set_target_properties("${RUNNER}" PROPERTIES LINK_FLAGS "-ldl -lpthread -lz -L ${SCRATCH_DIR}")
    endif()
  endforeach()
endif()
