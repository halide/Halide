# CMake toolchain setup for AArch32 baremetal target
# with semihosting mode enabled, where minimum I/O communication with a host PC is available
set(CROSS_PREFIX "arm-none-eabi-")
set(CMAKE_SYSTEM_NAME none)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# For newlib (standard C libraray) and semihosting mode
set(C_COMMON_FLAGS "-specs=rdimon.specs")

# Target CPU dependent flags to use NEON. Please modify for your target.
set(C_COMMON_FLAGS "${C_COMMON_FLAGS} -march=armv7-a -mfpu=neon -mfloat-abi=hard")

set(CMAKE_CXX_FLAGS "${C_COMMON_FLAGS}")
set(CMAKE_C_FLAGS "${C_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS "${C_COMMON_FLAGS}")

# To suppress linker warning "missing .note.GNU-stack section" by GCC 12
set(CMAKE_EXE_LINKER_FLAGS "-z noexecstack")

# Halide target for Halide Generator
set(Halide_TARGET "arm-32-noos-semihosting")

# To prevent Threads and DL libs from being linked to runtime, as this toolchain doesn't have them
set(Halide_RUNTIME_NO_THREADS ON)
set(Halide_RUNTIME_NO_DL_LIBS ON)

# Switch for baremetal specific build steps
set(BAREMETAL ON)
