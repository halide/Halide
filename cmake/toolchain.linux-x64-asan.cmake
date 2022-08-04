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

set(LLVM_INSTALL_DIR ${LLVM_DIR}/../../../)

set(CMAKE_C_COMPILER ${LLVM_INSTALL_DIR}/bin/clang)
set(CMAKE_CXX_COMPILER ${LLVM_INSTALL_DIR}/bin/clang++)

set(_ASAN_FLAGS "-fsanitize=address")
# set(_ASAN_FLAGS "-fsanitize=address -shared-libasan")

set(CMAKE_CXX_FLAGS_INIT ${_ASAN_FLAGS})
set(CMAKE_C_FLAGS_INIT ${_ASAN_FLAGS})

set(CMAKE_EXE_LINKER_FLAGS_INIT ${_ASAN_FLAGS})
set(CMAKE_SHARED_LINKER_FLAGS_INIT ${_ASAN_FLAGS})
set(CMAKE_MODULE_LINKER_FLAGS_INIT ${_ASAN_FLAGS})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Set to empty string to indicate the resulting binaries can be natively executed
set(CMAKE_CROSSCOMPILING_EMULATOR)

# A few downstream build rules need to be able to sniff these:
# - ASAN means we can't run with large stacks, so some things will overflow
set(Halide_ASAN_ENABLED ON)
# - Pretty much every sanitizer affects performance, so skip benchmarks
set(Halide_ANY_SANITIZERS_ENABLED ON)

# If running under ASAN, we need to suppress some errors:
# - detect_leaks, because circular Expr chains in Halide can indeed leak,
#   but we don't care here
# - detect_container_overflow, because this is a known false-positive
#   if compiling with a non-ASAN build of LLVM (which is usually the case)
set(SANITIZER_ENV_VARS "ASAN_OPTIONS=detect_leaks=0:detect_container_overflow=0")
# set(SANITIZER_ENV_VARS "ASAN_OPTIONS=detect_leaks=0:detect_container_overflow=0;LD_PRELOAD=${LLVM_INSTALL_DIR}/lib/clang/16.0.0/lib/x86_64-unknown-linux-gnu/libclang_rt.asan.so")

# Work around bug where "cmake -E env $FOO" gives error if FOO is empty
set(SANITIZER_SET_ENV_VARS env ${SANITIZER_ENV_VARS})

