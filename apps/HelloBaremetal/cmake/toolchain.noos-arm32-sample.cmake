# CMake toolchain setup for AArch32 baremetal target
# with semihosting mode enabled, where minimum I/O communication with a host PC is available
set(CMAKE_SYSTEM_NAME none)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_PREFIX "arm-none-eabi-")
set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# For newlib (standard C library) and semihosting mode
set(C_COMMON_FLAGS "-specs=rdimon.specs")

# Target CPU dependent flags to use NEON. Please modify for your target.
string(APPEND C_COMMON_FLAGS " -march=armv7-a -mfpu=neon -mfloat-abi=hard")

set(CMAKE_CXX_FLAGS_INIT "${C_COMMON_FLAGS}")
set(CMAKE_C_FLAGS_INIT "${C_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${C_COMMON_FLAGS}")

# Halide target for Halide Generator
set(Halide_TARGET "arm-32-noos-semihosting")

# To prevent Threads and DL libs from being linked to runtime, as this toolchain doesn't have them
set(Halide_RUNTIME_NO_THREADS ON)
set(Halide_RUNTIME_NO_DL_LIBS ON)

##
# noosrt.s (NEON enable trampoline + entropy syscall stub) is built here and
# linked into every executable via CMAKE_<LANG>_STANDARD_LIBRARIES, the same
# mechanism this toolchain already uses to link libgcc/libc into every
# target. It must stay a plain object, not archived into a .a: _getentropy is
# only demanded partway through the implicit -lc/-lrdimon GCC appends after
# everything else, by which point a plain archive would already be scanned.
##

set(_HelloBaremetal_support_obj "${CMAKE_BINARY_DIR}/HelloBaremetal-support/noosrt.o")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/HelloBaremetal-support")

separate_arguments(_HelloBaremetal_support_flags UNIX_COMMAND "${C_COMMON_FLAGS}")

execute_process(
    COMMAND
        "${CROSS_PREFIX}gcc" ${_HelloBaremetal_support_flags} -c
        "${CMAKE_CURRENT_LIST_DIR}/noosrt.s"
        -o "${_HelloBaremetal_support_obj}"
    COMMAND_ERROR_IS_FATAL ANY
)

set(CMAKE_C_STANDARD_LIBRARIES "${_HelloBaremetal_support_obj}")
set(CMAKE_CXX_STANDARD_LIBRARIES "${_HelloBaremetal_support_obj}")

# It is safe to run NEON-enablement in the boot-up process before main()
# (or the C runtime) starts.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-z noexecstack --entry=_enable_neon")
