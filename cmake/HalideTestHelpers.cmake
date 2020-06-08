##
# Remove the NDEBUG flag for the directory scope.
##

include(WipeStandardFlags)
wipe_standard_flags("[/-]D *NDEBUG")
wipe_standard_flags("[/-]O[^ ]+")

##
# Define helper targets for defining tests
##

if (NOT TARGET Halide::Test)
    # Capture common halide test features in a single target.
    add_library(Halide_test INTERFACE)
    add_library(Halide::Test ALIAS Halide_test)

    # Obviously, link to the main library
    target_link_libraries(Halide_test INTERFACE Halide::Halide)

    # Everyone gets to see the common headers
    target_include_directories(Halide_test
                               INTERFACE
                               ${Halide_SOURCE_DIR}/test/common
                               ${Halide_SOURCE_DIR}/tools)

    # Tests are built with the equivalent of OPTIMIZE_FOR_BUILD_TIME (-O0 or /Od).
    # Also allow tests, via conditional compilation, to use the entire
    # capability of the CPU being compiled on via -march=native. This
    # presumes tests are run on the same machine they are compiled on.
    target_compile_options(Halide_test INTERFACE
                           $<$<CXX_COMPILER_ID:MSVC>:/Od>
                           $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O0>
                           $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-march=native>)
endif ()

if (NOT TARGET Halide::ExpectAbort)
    # Add an OBJECT (not static) library to convert abort calls into exit(1).
    add_library(Halide_expect_abort OBJECT ${Halide_SOURCE_DIR}/test/common/expect_abort.cpp)
    add_library(Halide::ExpectAbort ALIAS Halide_expect_abort)
endif ()

##
# Convenience methods for defining tests.
##

function(add_halide_test TARGET)
    set(options EXPECT_FAILURE)
    set(oneValueArgs WORKING_DIRECTORY)
    set(multiValueArgs GROUPS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_test(NAME ${TARGET}
             COMMAND ${TARGET}
             WORKING_DIRECTORY "${args_WORKING_DIRECTORY}")

    set_tests_properties(${TARGET} PROPERTIES
                         LABELS "${args_GROUPS}"
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
    if (${args_EXPECT_FAILURE})
        set_tests_properties(${TARGET} PROPERTIES WILL_FAIL true)
    endif ()
endfunction()

function(tests)
    set(options EXPECT_FAILURE)
    set(oneValueArgs)
    set(multiValueArgs SOURCES GROUPS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    list(GET args_GROUPS 0 PRIMARY_GROUP)

    set(TEST_NAMES "")
    foreach (file IN LISTS args_SOURCES)
        get_filename_component(name "${file}" NAME_WE)
        set(TARGET "${PRIMARY_GROUP}_${name}")

        list(APPEND TEST_NAMES "${TARGET}")

        add_executable("${TARGET}" "${file}")
        target_link_libraries("${TARGET}" PRIVATE Halide::Test)
        if ("${file}" MATCHES ".cpp$")
            # Note: source for re-use may be overriden by tests_threaded
            target_precompile_headers("${TARGET}" REUSE_FROM _test_internal)
        endif ()

        if (args_EXPECT_FAILURE)
            add_halide_test("${TARGET}" GROUPS ${args_GROUPS} EXPECT_FAILURE)
            target_link_libraries("${TARGET}" PRIVATE Halide::ExpectAbort)
        else ()
            add_halide_test("${TARGET}" GROUPS ${args_GROUPS})
        endif ()
    endforeach ()

    set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction(tests)

function(tests_threaded)
    set(targets ${ARGN})
    # Override source target from where pre-compiled headers are re-used,
    # (can't re-use across non/threaded b/c compiler flags must match).
    set_property(TARGET ${targets} PROPERTY
        PRECOMPILE_HEADERS_REUSE_FROM _test_internal_threaded)

    target_link_libraries(${targets} PRIVATE Threads::Threads)
endfunction(tests_threaded)
