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

    # Obviously link to libHalide, but also grant all tests access to the threads library.
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

if (NOT TARGET Halide::TerminateHandler)
    # Add an OBJECT (not static) library to add a terminate_handler to catch unhandled exceptions.
    add_library(Halide_terminate_handler OBJECT ${Halide_SOURCE_DIR}/test/common/terminate_handler.cpp)
    add_library(Halide::TerminateHandler ALIAS Halide_terminate_handler)
endif ()

##
# Convenience methods for defining tests.
##

function(add_halide_test TARGET)
    set(options EXPECT_FAILURE USE_EXIT_CODE_ONLY)
    set(oneValueArgs WORKING_DIRECTORY)
    set(multiValueArgs GROUPS COMMAND ARGS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT args_COMMAND)
        set(args_COMMAND ${TARGET})
    endif ()

    add_test(NAME ${TARGET}
             COMMAND ${args_COMMAND} ${args_ARGS}
             WORKING_DIRECTORY "${args_WORKING_DIRECTORY}")
    set_halide_compiler_warnings(${TARGET})

    # We can't add Halide::TerminateHandler here, because it requires Halide::Error
    # and friends to be present in the final linkage, but some callers of add_halide_test()
    # are AOT tests, which don't link in libHalide. (It's relatively rare for these
    # tests to throw exceptions, though, so this isn't the dealbreaker you might think.)
    #
    # target_link_libraries("${TARGET}" PRIVATE Halide::TerminateHandler)

    set_tests_properties(${TARGET} PROPERTIES
                         LABELS "${args_GROUPS}"
                         ENVIRONMENT "HL_TARGET=${Halide_TARGET};HL_JIT_TARGET=${Halide_TARGET}"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
                         WILL_FAIL ${args_EXPECT_FAILURE})

    if (NOT args_USE_EXIT_CODE_ONLY)
        set_tests_properties(${TARGET} PROPERTIES
                             PASS_REGULAR_EXPRESSION "Success!")
    endif ()

    set_target_properties(${TARGET} PROPERTIES
                          CXX_VISIBILITY_PRESET hidden
                          VISIBILITY_INLINES_HIDDEN TRUE)


    if (WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING)
        if (WITH_SERIALIZATION)
            target_compile_definitions(${TARGET} PRIVATE WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING)
        endif ()
    endif ()

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
    set(options EXPECT_FAILURE USE_EXIT_CODE_ONLY)
    set(oneValueArgs)
    set(multiValueArgs SOURCES GROUPS ARGS)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    list(GET args_GROUPS 0 PRIMARY_GROUP)

    set(TEST_NAMES "")
    foreach (file IN LISTS args_SOURCES)
        cmake_path(GET file STEM name)
        set(TARGET "${PRIMARY_GROUP}_${name}")

        list(APPEND TEST_NAMES "${TARGET}")

        add_executable("${TARGET}" "${file}")
        target_link_libraries("${TARGET}" PRIVATE Halide::Test Halide::TerminateHandler)
        if ("${file}" MATCHES ".cpp$")
            target_precompile_headers("${TARGET}" REUSE_FROM _test_internal)
        endif ()

        if (args_EXPECT_FAILURE)
            target_link_libraries("${TARGET}" PRIVATE Halide::ExpectAbort)
            set(args_EXPECT_FAILURE EXPECT_FAILURE)
        else ()
            set(args_EXPECT_FAILURE "")
        endif ()

        if (args_USE_EXIT_CODE_ONLY)
            set(args_USE_EXIT_CODE_ONLY USE_EXIT_CODE_ONLY)
        else ()
            set(args_USE_EXIT_CODE_ONLY "")
        endif ()

        add_halide_test("${TARGET}" ARGS ${args_ARGS} GROUPS ${args_GROUPS} ${args_EXPECT_FAILURE} ${args_USE_EXIT_CODE_ONLY})
    endforeach ()

    set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction()
