cmake_minimum_required(VERSION 3.16)
project(runtime_tests)

enable_testing()

set(RUNTIME_SOURCE_DIR ${Halide_SOURCE_DIR}/src/runtime)

set(RUNTIME_TESTS_CPP 
    block_allocator.cpp
    block_storage.cpp
    memory_arena.cpp
    string_storage.cpp
    linked_list.cpp
    string_table.cpp
)

# Note: ensure that these flags match the flags in the Makefile.
# Note: this always uses Clang-from-LLVM for compilation, so none of these flags should need conditionalization.
set(RUNTIME_TESTS_CXX_FLAGS
    -O3
    -std=c++17
    -fno-blocks
    -fno-exceptions
    -fno-unwind-tables
    -fno-vectorize
    # Note: we don't want static locals to get thread synchronization stuff.
    -fno-threadsafe-statics
    # Necessary for using virtual functions in the runtime code.
    -fno-rtti
    -Wall
    -Wcast-qual
    -Werror
    -Wignored-qualifiers
    -Wno-comment
    -Wno-psabi
    -Wno-unknown-warning-option
    -Wno-unused-function
    -Wsign-compare
)

set(RUNTIME_TESTS_ARCH_BITS 32)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(RUNTIME_TESTS_ARCH_BITS 64)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86")
        set(RUNTIME_TESTS_SYSTEM_CPP
            ${RUNTIME_SOURCE_DIR}/alignment_${RUNTIME_TESTS_ARCH_BITS}.cpp
            ${RUNTIME_SOURCE_DIR}/allocation_cache.cpp
            ${RUNTIME_SOURCE_DIR}/cache.cpp
            ${RUNTIME_SOURCE_DIR}/can_use_target.cpp
            ${RUNTIME_SOURCE_DIR}/destructors.cpp
            ${RUNTIME_SOURCE_DIR}/device_interface.cpp
            ${RUNTIME_SOURCE_DIR}/errors.cpp
            ${RUNTIME_SOURCE_DIR}/fake_get_symbol.cpp
            ${RUNTIME_SOURCE_DIR}/fake_thread_pool.cpp
            ${RUNTIME_SOURCE_DIR}/float16_t.cpp
            ${RUNTIME_SOURCE_DIR}/halide_buffer_t.cpp
            ${RUNTIME_SOURCE_DIR}/linux_clock.cpp
            ${RUNTIME_SOURCE_DIR}/linux_host_cpu_count.cpp
            ${RUNTIME_SOURCE_DIR}/linux_yield.cpp
            ${RUNTIME_SOURCE_DIR}/metadata.cpp
            ${RUNTIME_SOURCE_DIR}/msan_stubs.cpp
            ${RUNTIME_SOURCE_DIR}/posix_allocator.cpp
            ${RUNTIME_SOURCE_DIR}/posix_clock.cpp
            ${RUNTIME_SOURCE_DIR}/posix_error_handler.cpp
            ${RUNTIME_SOURCE_DIR}/posix_get_symbol.cpp
            ${RUNTIME_SOURCE_DIR}/posix_io.cpp
            ${RUNTIME_SOURCE_DIR}/posix_print.cpp
            ${RUNTIME_SOURCE_DIR}/posix_threads.cpp
            ${RUNTIME_SOURCE_DIR}/prefetch.cpp
            ${RUNTIME_SOURCE_DIR}/pseudostack.cpp
            ${RUNTIME_SOURCE_DIR}/to_string.cpp
            ${RUNTIME_SOURCE_DIR}/write_debug_image.cpp
        )

        set(RUNTIME_TESTS_SYSTEM_LFLAGS -lc -ldl -lpthread -lrt)
    endif()

    foreach (file IN LISTS RUNTIME_TESTS_CPP)

        set(RUNTIME_TESTS_SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${file}")

        set(RUNTIME_TESTS_DEFINES 
            -DCOMPILING_HALIDE_RUNTIME
            -DCOMPILING_HALIDE_RUNTIME_TESTS 
            -DBITS_${RUNTIME_TESTS_ARCH_BITS}
            -DHALIDE_VERSION=${Halide_VERSION} 
            -DHALIDE_VERSION_MAJOR=${Halide_VERSION_MAJOR}
            -DHALIDE_VERSION_MINOR=${Halide_VERSION_MINOR}
            -DHALIDE_VERSION_PATCH=${Halide_VERSION_PATCH}
            -I${Halide_SOURCE_DIR}/src/runtime
            -I${Halide_SOURCE_DIR}/test/common)

        set(RUNTIME_TESTS_DEFINES_debug -g -DDEBUG_RUNTIME ${RUNTIME_TESTS_DEFINES})

        foreach (SUFFIX IN ITEMS "" "_debug")
            set(RUNTIME_TESTS_SYSTEM_CLANG_FLAGS ${RUNTIME_TESTS_CXX_FLAGS} ${RUNTIME_TESTS_DEFINES${SUFFIX}} -m${RUNTIME_TESTS_ARCH_BITS} )

            get_filename_component(name "${file}" NAME_WE)
            set(basename "${name}_${RUNTIME_TESTS_ARCH_BITS}${SUFFIX}")

            add_custom_command(OUTPUT ${basename}
                            COMMAND clang ${RUNTIME_TESTS_SYSTEM_CLANG_FLAGS} ${RUNTIME_TESTS_SYSTEM_LFLAGS} -o "${basename}" "$<SHELL_PATH:${RUNTIME_TESTS_SOURCE_FILE}>" ${RUNTIME_TESTS_SYSTEM_CPP}
                            DEPENDS "${RUNTIME_TESTS_SOURCE_FILE}" ${RUNTIME_TESTS_SYSTEM_CPP}
                            VERBATIM)

            add_custom_target(${basename}_target DEPENDS ${basename} ${RUNTIME_TESTS_SOURCE_FILE} ${RUNTIME_TESTS_SYSTEM_CPP})
            add_test(NAME ${basename}_test COMMAND ${basename} )
            add_dependencies(_test_internal ${basename}_target)

        endforeach ()
    endforeach ()
else()
    message(SEND_ERROR "Unsupported system configuration for runtime tests!")
endif()
    

