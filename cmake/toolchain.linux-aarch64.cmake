# Toolchain for cross-compiling to Linux-aarch64 on a Linux-x86-64 host.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if (NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
endif ()
if (NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif ()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(QEMU_AARCH64 qemu-aarch64)
if (QEMU_AARCH64 AND EXISTS "/usr/aarch64-linux-gnu")
    set(CMAKE_CROSSCOMPILING_EMULATOR ${QEMU_AARCH64} -L /usr/aarch64-linux-gnu)
endif ()
