# Toolchain for compiling with ASAN enabled on a Linux-x86-64 host.
# This is done as a "crosscompile" because we must use our LLVM version
# of clang for *all* compilation (rather than using it just for bitcode
# and letting the host compiler, eg gcc, handle everything else); fuzzer
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

set(CMAKE_C_FLAGS_INIT "-fsanitize=fuzzer-no-link")
set(CMAKE_CXX_FLAGS_INIT "-fsanitize=fuzzer-no-link")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=${CMAKE_LINKER}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=${CMAKE_LINKER}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=${CMAKE_LINKER}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/env)

# Sanitizer builds intermittently fail when using CCache for reasons that aren't
# clear; default to turning it off
set(Halide_CCACHE_BUILD OFF)
