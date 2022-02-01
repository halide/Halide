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

##
# Convenience method for defining test cases that use generators. 
##

function(halide_define_aot_test NAME)
    set(options OMIT_DEFAULT_GENERATOR)
    set(oneValueArgs FUNCTION_NAME GROUP)
    set(multiValueArgs GEN_DEPS EXTRA_LIBS ENABLE_IF FEATURES PARAMS GEN_TARGET)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    add_executable("${NAME}.generator" "${NAME}_generator.cpp")
    target_link_libraries("${NAME}.generator" PRIVATE Halide::Generator ${args_GEN_DEPS})

    set(TARGET "generator_aot_${NAME}")
    set(DEPS ${args_EXTRA_LIBS})
    if (NOT args_OMIT_DEFAULT_GENERATOR)
        add_halide_library(${NAME}
                           FROM "${NAME}.generator"
                           PARAMS "${args_PARAMS}"
                           TARGETS "${args_GEN_TARGET}"
                           FUNCTION_NAME "${args_FUNCTION_NAME}"
                           FEATURES "${args_FEATURES}")
        list(APPEND DEPS ${NAME} ${NAME}.runtime)
    endif ()

    if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
        add_wasm_executable("${TARGET}"
                            SRCS "${NAME}_aottest.cpp"
                            DEPS "${DEPS}"
                            INCLUDES
                            "${Halide_BINARY_DIR}/include"
                            "${Halide_SOURCE_DIR}/test/common"
                            "${Halide_SOURCE_DIR}/tools"
                            "${CMAKE_CURRENT_BINARY_DIR}")

        add_wasm_halide_test("${TARGET}" GROUPS generator)
    else ()
        add_executable("${TARGET}" "${NAME}_aottest.cpp")
        target_include_directories("${TARGET}" PRIVATE
            "${Halide_SOURCE_DIR}/test/common"
            "${Halide_SOURCE_DIR}/tools")
        if (NOT args_OMIT_DEFAULT_GENERATOR)
            target_link_libraries(${TARGET} PRIVATE ${NAME})
        endif ()
        if (args_EXTRA_LIBS)
            target_link_libraries(${TARGET} PRIVATE ${args_EXTRA_LIBS})
        endif ()

        # TODO(#4938): remove need for these definitions
        if ("${Halide_TARGET}" MATCHES "opencl")
            target_compile_definitions("${TARGET}" PRIVATE TEST_OPENCL)
        endif ()
        if ("${Halide_TARGET}" MATCHES "metal")
            target_compile_definitions("${TARGET}" PRIVATE TEST_METAL)
        endif ()
        if ("${Halide_TARGET}" MATCHES "cuda")
            target_compile_definitions("${TARGET}" PRIVATE TEST_CUDA)
        endif ()
        add_halide_test("${TARGET}" GROUPS "${args_GROUP}")
    endif ()
endfunction()


