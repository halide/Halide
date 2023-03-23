function(_set_target_options NAME)
    target_include_directories(${NAME} PRIVATE "${Halide_SOURCE_DIR}/src" "${Halide_SOURCE_DIR}/src/runtime")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Halide runtime lib has declarations for memcmp etc that conflict with GNU stdlib
        target_compile_options(${NAME} PRIVATE -Wno-builtin-declaration-mismatch )
    endif()
    math(EXPR bits "8 * ${CMAKE_SIZEOF_VOID_P}")
    target_compile_definitions(
        ${NAME}
        PRIVATE
        HALIDE_VERSION=${Halide_VERSION}
        HALIDE_VERSION_MAJOR=${Halide_VERSION_MAJOR}
        HALIDE_VERSION_MINOR=${Halide_VERSION_MINOR}
        HALIDE_VERSION_PATCH=${Halide_VERSION_PATCH}
        BITS_${bits}
        COMPILING_HALIDE_RUNTIME
        COMPILING_HALIDE_RUNTIME_TESTS
    )
endfunction()

function(halide_define_runtime_internal_test NAME)
    add_executable(runtime_internal_${NAME} ${NAME}.cpp $<TARGET_OBJECTS:runtime_internal_common>)
    _set_target_options(runtime_internal_${NAME})
    target_link_libraries(runtime_internal_${NAME} PRIVATE Halide::Test)
    add_halide_test(runtime_internal_${NAME} GROUPS runtime_internal)
endfunction()

# NOTE: These tests directly include runtime_internal.h which isn't compatible with MSVC
if(NOT MSVC)
    # Weak linkages are easier to get right with OBJECT libraries
    add_library(runtime_internal_common OBJECT
                common.cpp
                "${Halide_SOURCE_DIR}/src/runtime/msan_stubs.cpp"
                "${Halide_SOURCE_DIR}/src/runtime/to_string.cpp")
    _set_target_options(runtime_internal_common)

    halide_define_runtime_internal_test(block_allocator)
    halide_define_runtime_internal_test(block_storage)
    halide_define_runtime_internal_test(linked_list)
    halide_define_runtime_internal_test(memory_arena)
    halide_define_runtime_internal_test(string_storage)
    halide_define_runtime_internal_test(string_table)
endif()
