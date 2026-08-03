##
# Tests for the standalone `halide.runtime` module.
##

# Register a Halide generator (once). Separated from kernel building so a single
# generator can be compiled for several targets (e.g. the GPU backends below).
function(add_runtime_test_generator GEN SRC)
    add_halide_generator(${GEN}_gen SOURCES "${SRC}")
endfunction()

# Build an AOT kernel and wrap it into a loadable shared module named "<MOD>.so".
# Unlike a Python extension library (which hides all but PyInit_), we keep the
# filter's `<MOD>_argv` / `<MOD>_metadata` symbols exported so the runtime loader
# can dlsym them; WHOLE_ARCHIVE pulls in the filter objects even though the stub
# references nothing. The loader defaults the filter name to the file stem, so we
# name the module and the Halide function identically.
function(add_runtime_test_kernel MOD)
    cmake_parse_arguments(ARG "" "GENERATOR" "FEATURES;PARAMS" ${ARGN})
    if (NOT ARG_GENERATOR)
        set(ARG_GENERATOR ${MOD})
    endif ()

    add_halide_library(${MOD}_aot
        FROM ${ARG_GENERATOR}_gen
        GENERATOR ${ARG_GENERATOR}
        FUNCTION_NAME ${MOD}
        FEATURES ${ARG_FEATURES}
        PARAMS ${ARG_PARAMS}
    )

    add_library(${MOD}_module MODULE aot_module_stub.c)
    target_link_libraries(${MOD}_module PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${MOD}_aot>")
    set_target_properties(
        ${MOD}_module
        PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${MOD}
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
    )
endfunction()

# A tiny kernel: load, call, and Buffer interop.
add_runtime_test_generator(runtimeadd runtimeadd_generator.cpp)
add_runtime_test_kernel(runtimeadd)
add_python_test(
    FILE load_aot.py
    LABEL python_runtime
    TEST_ARGS "$<TARGET_FILE:runtimeadd_module>"
)

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
# We compile the same GPU-scheduled kernel for each backend that can be built on
# this platform, then run one test that tries each and skips a backend at run
# time if its device/driver is missing (as on a headless CI runner).
#
#  * Metal    -- Apple only (its runtime links Apple frameworks).
#  * OpenCL   -- OpenCL C is emitted and compiled by the driver at run time;
#                the runtime is dlopen-based, so it builds everywhere.
#  * Vulkan   -- SPIR-V is emitted directly (no LLVM GPU target); vk_int8 is
#                required because the kernel uses 8-bit buffers.
#  * CUDA     -- needs LLVM's NVPTX backend to emit PTX.
##

add_runtime_test_generator(gpuadd gpu_generator.cpp)

set(gpu_modules "")

if (APPLE)
    add_runtime_test_kernel(gpuadd_metal GENERATOR gpuadd FEATURES metal)
    list(APPEND gpu_modules gpuadd_metal)
endif ()

add_runtime_test_kernel(gpuadd_opencl GENERATOR gpuadd FEATURES opencl)
list(APPEND gpu_modules gpuadd_opencl)

add_runtime_test_kernel(gpuadd_vulkan GENERATOR gpuadd FEATURES vulkan vk_int8)
list(APPEND gpu_modules gpuadd_vulkan)

if ("NVPTX" IN_LIST Halide_LLVM_COMPONENTS)
    add_runtime_test_kernel(gpuadd_cuda GENERATOR gpuadd FEATURES cuda)
    list(APPEND gpu_modules gpuadd_cuda)
else ()
    message(STATUS "halide.runtime GPU test: skipping CUDA (LLVM has no NVPTX backend)")
endif ()

set(gpu_module_files "")
foreach (mod IN LISTS gpu_modules)
    list(APPEND gpu_module_files "$<TARGET_FILE:${mod}_module>")
endforeach ()

add_python_test(
    FILE gpu.py
    LABEL python_runtime
    TEST_ARGS ${gpu_module_files}
)

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
