# Toolchain for compiling with ASAN enabled on a Linux-x86-64 host.
# This is done as a "crosscompile" because we must use our LLVM version
# of clang for *all* compilation (rather than using it just for bitcode
# and letting the host compiler, eg gcc, handle everything else); ASAN
# essentially requires everything to be compiled with matching versions
# of the same compiler.
#
# Note: requires LLVM to be built with -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind"
#
# Note: only tested with LLVM/Clang 16 as of this comment. Earlier versions
# may likely work but are not tested.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_COMPILER ${LLVM_ROOT}/bin/clang)
set(CMAKE_CXX_COMPILER ${LLVM_ROOT}/bin/clang++)
set(CMAKE_LINKER ${LLVM_ROOT}/bin/ld.lld)

set(CMAKE_C_FLAGS_INIT "-fsanitize=address")
set(CMAKE_CXX_FLAGS_INIT "-fsanitize=address")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=${CMAKE_LINKER}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=${CMAKE_LINKER}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=${CMAKE_LINKER}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/env)

# Can't mix -fsanitize=address with -fsanitize=fuzzer
set(WITH_TEST_FUZZ OFF)

# Sanitizer builds intermittently fail when using CCache for reasons that aren't
# clear; default to turning it off
set(Halide_CCACHE_BUILD OFF)

if (NOT DEFINED Halide_SHARED_ASAN_RUNTIME_LIBRARY)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} "-print-file-name=libclang_rt.asan.so"
        OUTPUT_VARIABLE Halide_SHARED_ASAN_RUNTIME_LIBRARY
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif ()

set(Halide_SHARED_ASAN_RUNTIME_LIBRARY "${Halide_SHARED_ASAN_RUNTIME_LIBRARY}"
    CACHE FILEPATH "Library to preload when running Python tests.")

set(
    Halide_PYTHON_LAUNCHER
    ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD=${Halide_SHARED_ASAN_RUNTIME_LIBRARY}
)

