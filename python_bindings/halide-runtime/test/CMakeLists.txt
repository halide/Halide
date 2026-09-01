##
# Tests for the standalone `halide.runtime` module.
##

# Register a Halide generator (once). Separated from kernel building so a single
# generator can be compiled for several targets (e.g. the GPU backends below).
function(add_runtime_test_generator GEN SRC)
    add_halide_generator(${GEN}_gen SOURCES "${SRC}")
endfunction()

# Build an AOT kernel and wrap it into a loadable shared module named "<MOD>.so".
# WHOLE_ARCHIVE pulls in the filter objects even though the stub references
# nothing. The loader defaults the filter name to the file stem, so we name the
# module and the Halide function identically.
function(add_runtime_test_kernel MOD)
    cmake_parse_arguments(ARG "" "GENERATOR" "PARAMS" ${ARGN})
    if (NOT ARG_GENERATOR)
        set(ARG_GENERATOR ${MOD})
    endif ()

    add_halide_library(
        ${MOD}_aot
        FROM ${ARG_GENERATOR}_gen
        GENERATOR ${ARG_GENERATOR}
        FUNCTION_NAME ${MOD}
        PARAMS ${ARG_PARAMS}
    )

    add_library(${MOD}_module MODULE aot_module_stub.c)
    target_link_libraries(${MOD}_module PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${MOD}_aot>")
    set_target_properties(${MOD}_module PROPERTIES PREFIX "" OUTPUT_NAME ${MOD})

    if (MSVC)
        # link.exe's incremental linker can corrupt a /WHOLEARCHIVE module
        # (LNK1236: corrupt or invalid COFF sections) when relinking it against
        # a changed archive in an existing build tree. These modules are only
        # ever built once per configuration, so incremental linking buys us
        # nothing and isn't worth the risk.
        target_link_options(${MOD}_module PRIVATE /INCREMENTAL:NO)
    endif ()
endfunction()

# A tiny kernel: load, call, and Buffer interop.
add_runtime_test_generator(runtimeadd runtimeadd_generator.cpp)
add_runtime_test_kernel(runtimeadd)
add_python_test(FILE load_aot.py LABEL python_runtime TEST_ARGS "$<TARGET_FILE:runtimeadd_module>")

# The full scalar/buffer calling convention: every scalar type, a 2-D buffer,
# and multiple outputs including a Tuple output. We compile the generator twice
# with different values of its enum GeneratorParam `combine` (a compile-time
# choice, baked into each kernel) to show that it selects behavior without
# changing the runtime calling convention.
add_runtime_test_generator(callconv callconv_generator.cpp)
add_runtime_test_kernel(callconv)                              # combine=add (default)
add_runtime_test_kernel(callconv_xor GENERATOR callconv PARAMS combine=xor)
add_python_test(
    FILE call_convention.py
    LABEL python_runtime
    TEST_ARGS "$<TARGET_FILE:callconv_module>" "$<TARGET_FILE:callconv_xor_module>"
)

##
# GPU coverage.
#
# Build and run the GPU kernel only when the active target selects a supported
# GPU backend. If its device or driver is unavailable (as on a headless CI
# runner), the Python test reports a skip.
##

if ("${Halide_TARGET}" MATCHES "(^|[-;])(metal|opencl|vulkan|cuda)([-;]|$)")
    add_runtime_test_generator(gpuadd gpu_generator.cpp)
    add_runtime_test_kernel(gpuadd)
    add_python_test(FILE gpu.py LABEL python_runtime TEST_ARGS "$<TARGET_FILE:gpuadd_module>")
endif ()

# The whole point of the runtime module: it must not depend on libHalide.
# (Symbol-visibility trick from the export-single-symbol test; POSIX only.)
if (NOT MSVC AND NOT WIN32)
    add_test(
        NAME python_runtime_no_libhalide
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMODULE=$<TARGET_FILE:Halide_PythonRuntime>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_no_libhalide.cmake"
    )
    set_tests_properties(python_runtime_no_libhalide PROPERTIES LABELS "python;python_runtime")
endif ()
