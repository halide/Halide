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

    # Obviously link to libHalide, but also grant all tests access to the
    # platform threads library.
    target_link_libraries(Halide_test INTERFACE Halide::Halide Threads::Threads)

    # Make internal_assert, debug, etc. available to tests
    target_compile_definitions(Halide_test INTERFACE HALIDE_KEEP_MACROS)

    # Disable warnings about standard C functions that have more secure replacements
    # in the Windows API.
    target_compile_definitions(
        Halide_test
        INTERFACE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    )

    # Everyone gets to see the common headers
    target_include_directories(
        Halide_test
        INTERFACE
        ${Halide_SOURCE_DIR}/test/common
        ${Halide_SOURCE_DIR}/tools
    )
endif ()

if (NOT TARGET Halide::ExpectAbort)
    # Add an OBJECT (not static) library to convert abort calls into exit(1).
    add_library(Halide_expect_abort OBJECT ${Halide_SOURCE_DIR}/test/common/expect_abort.cpp)
    add_library(Halide::ExpectAbort ALIAS Halide_expect_abort)
    target_link_libraries(Halide_expect_abort PRIVATE Halide::Halide)
endif ()

if (NOT TARGET Halide::TerminateHandler)
    # Add an OBJECT (not static) library to add a terminate_handler to catch unhandled exceptions.
    add_library(
        Halide_terminate_handler OBJECT
        ${Halide_SOURCE_DIR}/test/common/terminate_handler.cpp
    )
    add_library(Halide::TerminateHandler ALIAS Halide_terminate_handler)
endif ()

##
# Convenience methods for defining tests.
##

function(add_test_labels)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "TESTS;LABELS")

    foreach (test IN LISTS ARG_TESTS)
        get_test_property("${test}" LABELS labels)
        list(APPEND labels ${ARG_LABELS})
        set_tests_properties("${test}" PROPERTIES LABELS "${labels}")
    endforeach ()

    # Add a meta-target for each group, to allow us to build by group easily
    foreach (label IN LISTS ARG_LABELS)
        set(meta_target "build_${label}")
        if (NOT TARGET "${meta_target}")
            add_custom_target("${meta_target}")
        endif ()
        add_dependencies("${meta_target}" ${ARG_TESTS})
    endforeach ()
endfunction()

function(add_halide_test TARGET)
    set(options EXPECT_FAILURE USE_EXIT_CODE_ONLY)
    set(oneValueArgs WORKING_DIRECTORY)
    set(multiValueArgs GROUPS COMMAND ARGS)
    cmake_parse_arguments(PARSE_ARGV 1 args "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if (NOT args_COMMAND)
        set(args_COMMAND "${TARGET}")
    endif ()

    add_test(
        NAME ${TARGET}
        COMMAND ${args_COMMAND} ${args_ARGS}
        WORKING_DIRECTORY "${args_WORKING_DIRECTORY}"
    )
    if (NOT Halide_TARGET MATCHES "wasm")
        set_halide_compiler_warnings(${TARGET})
    endif ()

    # We can't add Halide::TerminateHandler here, because it requires
    # Halide::Error and friends to be present in the final linkage, but some
    # callers of add_halide_test() are AOT tests, which don't link in
    # libHalide. (It's relatively rare for these tests to throw exceptions,
    # though, so this isn't the deal-breaker you might think.)
    #
    # target_link_libraries("${TARGET}" PRIVATE Halide::TerminateHandler)

    # Resolve the "cmake" meta-target
    string(REGEX REPLACE "^cmake" "${Halide_CMAKE_TARGET}" _resolved_target "${Halide_TARGET}")

    set_tests_properties(
        ${TARGET}
        PROPERTIES
        LABELS "${args_GROUPS}"
        ENVIRONMENT "HL_TARGET=${_resolved_target};HL_JIT_TARGET=${_resolved_target}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP(-WITH-ISSUE(-[0-9]+)?)?\\]"
        WILL_FAIL ${args_EXPECT_FAILURE}
    )
    if ("multithreaded" IN_LIST args_GROUPS)
        set_tests_properties(${TARGET} PROPERTIES RUN_SERIAL TRUE)
    endif ()

    if (NOT args_USE_EXIT_CODE_ONLY)
        set_tests_properties(${TARGET} PROPERTIES PASS_REGULAR_EXPRESSION "Success!")
    endif ()

    set_target_properties(${TARGET}
        PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
    )


    if (WITH_SERIALIZATION AND WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING)
        if (NOT Halide_TARGET MATCHES "wasm")
            target_compile_definitions(${TARGET} PRIVATE WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING)
        endif ()
    endif ()

    add_test_labels(TESTS ${TARGET} LABELS ${args_GROUPS})
endfunction()

function(tests)
    set(options EXPECT_FAILURE USE_EXIT_CODE_ONLY)
    set(multiValueArgs SOURCES GROUPS ARGS)
    cmake_parse_arguments(PARSE_ARGV 0 args "${options}" "" "${multiValueArgs}")

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

        add_halide_test(
            "${TARGET}"
            ARGS ${args_ARGS}
            GROUPS
            ${args_GROUPS}  #
            ${args_EXPECT_FAILURE} ${args_USE_EXIT_CODE_ONLY}
        )
    endforeach ()

    set(TEST_NAMES "${TEST_NAMES}" PARENT_SCOPE)
endfunction()
