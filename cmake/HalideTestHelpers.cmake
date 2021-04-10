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
    target_link_libraries(Halide_test INTERFACE Halide::Halide Threads::Threads)

    # Everyone gets to see the common headers
    target_include_directories(Halide_test
                               INTERFACE
                               ${Halide_SOURCE_DIR}/test/common
                               ${Halide_SOURCE_DIR}/tools)
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
    set(multiValueArgs GROUPS COMMAND ARGS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT args_COMMAND)
        set(args_COMMAND ${TARGET})
    endif ()

    add_test(NAME ${TARGET}
             COMMAND ${args_COMMAND} ${args_ARGS}
             WORKING_DIRECTORY "${args_WORKING_DIRECTORY}")

    set_tests_properties(${TARGET} PROPERTIES
                         LABELS "${args_GROUPS}"
                         ENVIRONMENT "HL_TARGET=${Halide_TARGET};HL_JIT_TARGET=${Halide_TARGET}"
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
                         WILL_FAIL ${args_EXPECT_FAILURE})

    # Add a meta-target for each group, to allow us to build by group easily
    foreach (GROUP IN LISTS args_GROUPS)
        set(META_TARGET build_${GROUP})
        if (NOT TARGET ${META_TARGET})
            add_custom_target(${META_TARGET})
        endif ()
        add_dependencies(${META_TARGET} ${TARGET})
    endforeach ()

endfunction()

function(tests)
    set(options EXPECT_FAILURE)
    set(oneValueArgs)
    set(multiValueArgs SOURCES GROUPS ARGS)
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
            target_precompile_headers("${TARGET}" REUSE_FROM _test_internal)
        endif ()

        if (args_EXPECT_FAILURE)
            add_halide_test("${TARGET}" ARGS ${args_ARGS} GROUPS ${args_GROUPS} EXPECT_FAILURE)
            target_link_libraries("${TARGET}" PRIVATE Halide::ExpectAbort)
        else ()
            add_halide_test("${TARGET}" ARGS ${args_ARGS} GROUPS ${args_GROUPS})
        endif ()
    endforeach ()

    set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction()
