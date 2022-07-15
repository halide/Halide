function(halide_define_runtime_internal_test NAME)
    add_executable(runtime_internal_${NAME} ${NAME}.cpp)
    target_link_libraries(runtime_internal_${NAME} PRIVATE Halide::Test)
    target_include_directories(runtime_internal_${NAME} PRIVATE "${Halide_SOURCE_DIR}/src")
    target_include_directories(runtime_internal_${NAME} PRIVATE "${Halide_SOURCE_DIR}/src/runtime")
    target_link_libraries(runtime_internal_${NAME} PRIVATE Halide::Runtime)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Halide runtime lib has declarations for memcmp etc that conflict with GNU stdlib
        target_compile_options(runtime_internal_${NAME} PRIVATE -Wno-builtin-declaration-mismatch )
    endif()
    target_compile_definitions(
        runtime_internal_${NAME}
        PRIVATE
        HALIDE_VERSION=${Halide_VERSION}
        HALIDE_VERSION_MAJOR=${Halide_VERSION_MAJOR}
        HALIDE_VERSION_MINOR=${Halide_VERSION_MINOR}
        HALIDE_VERSION_PATCH=${Halide_VERSION_PATCH}
        COMPILING_HALIDE_RUNTIME
        COMPILING_HALIDE_RUNTIME_TESTS
    )    
    add_halide_test(runtime_internal_${NAME} GROUPS runtime_internal)
endfunction()

# NOTE: These tests directly include runtime_internal.h which isn't compatible with MSVC
if(NOT MSVC)
    halide_define_runtime_internal_test(block_allocator)
    halide_define_runtime_internal_test(block_storage)
    halide_define_runtime_internal_test(linked_list)
    halide_define_runtime_internal_test(memory_arena)
    halide_define_runtime_internal_test(string_storage)
    halide_define_runtime_internal_test(string_table)
endif()