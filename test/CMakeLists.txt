function(tests folder)
    file(GLOB TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")
    set(TEST_NAMES "")
    foreach (file ${TESTS})
        get_filename_component(name "${file}" NAME_WLE)

        set(TARGET "${folder}_${name}")
        list(APPEND TEST_NAMES "${TARGET}")

        halide_project("${TARGET}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
        target_compile_definitions("${TARGET}" PRIVATE "-DLLVM_VERSION=${LLVM_VERSION}")
        # Tests are built with the equivalent of OPTIMIZE_FOR_BUILD_TIME (-O0 or /Od).
        if (MSVC)
            target_compile_options("${TARGET}" PRIVATE "$<$<CONFIG:RELEASE>:/Od>")
        else ()
            target_compile_options("${TARGET}" PRIVATE "$<$<CONFIG:RELEASE>:-O0>")
            # Also allow tests, via conditional compilation, to use the entire
            # capability of the CPU being compiled on via -march=native. This
            # presumes tests are run on the same machine they are compiled on.
            target_compile_options("${TARGET}" PRIVATE "-march=native")
        endif ()

        target_include_directories("${TARGET}" PRIVATE "${HALIDE_INCLUDE_DIR}")
        target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}")
        if ("${folder}" STREQUAL "opengl")
            target_include_directories("${TARGET}" PRIVATE "${CMAKE_SOURCE_DIR}/src")
        endif ()

        set(GROUPS "${folder}")
        if ("${folder}" STREQUAL "performance" OR "${folder}" STREQUAL "auto_schedule" OR "${folder}" STREQUAL "opengl")
            # These shouldn't be part of run_tests, since they must be run with -j for timing purposes
            # Exception: opengl is excluded because Makefile excludes it too. Why?
        else ()
            list(APPEND GROUPS all)
        endif ()

        if ("${folder}" STREQUAL "error")
            add_halide_test("${TARGET}" GROUPS ${GROUPS} EXPECT_FAILURE)
        else ()
            add_halide_test("${TARGET}" GROUPS ${GROUPS})
        endif ()
    endforeach ()
    set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction(tests)

halide_project(_test_internal internal internal.cpp)
target_include_directories(_test_internal PRIVATE "${Halide_SOURCE_DIR}/src")
add_halide_test(_test_internal GROUPS internal)

option(WITH_TEST_AUTO_SCHEDULE "Build auto_schedule tests" ON)
if (WITH_TEST_AUTO_SCHEDULE)
    add_subdirectory(auto_schedule)
endif ()

option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
if (WITH_TEST_CORRECTNESS)
    add_subdirectory(correctness)
endif ()

option(WITH_TEST_ERROR "Build error tests" ON)
if (WITH_TEST_ERROR)
    add_subdirectory(error)
endif ()

option(WITH_TEST_WARNING "Build warning tests" ON)
if (WITH_TEST_WARNING)
    add_subdirectory(warning)
endif ()

option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
if (WITH_TEST_PERFORMANCE)
    add_subdirectory(performance)
endif ()

option(WITH_TEST_OPENGL "Build OpenGL tests" ON)
if (WITH_TEST_OPENGL)
    add_subdirectory(opengl)
endif ()

option(WITH_TEST_GENERATOR "Build Generator tests" ON)
if (WITH_TEST_GENERATOR)
    add_subdirectory(generator)
endif ()
