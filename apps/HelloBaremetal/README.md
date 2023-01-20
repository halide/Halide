# HelloBaremetal
## Overview
This is a sample application which demonstrates how to cross-compile the application with Halide generated function for baremetal target system without OS.

## Setup
There are wide varieties of baremetal system and this application is created just for one of them. More specifically, it is tested with the following conditions.
- Compiled by [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (version 12.2) AArch32 bare-metal target (`arm-none-eabi`)
- Targeted for Arm 32 bit CPU with NEON (Cortex-A9) on Arm Realview boards on [QEMU Arm System emulator](https://www.qemu.org/docs/master/system/target-arm.html) (version 7.2.50)
- With "semihosting" mode, where some limited I/O communication is available between host and target (e.g. printf() to stdout)

Because we cannot rely on the abstraction of OS, you will probably need to modify some parts if you target for different setup.

## How to build
### Cross compilation
Halide cross-compiling in CMake is tricky and there are a couple of ways to realize it. This application demonstrates 3 different ways with concrete code, located in `CMakeLists.txt` in the following sub directories.
1. cmake-twice
1. cmake-super_build
1. cmake-external_project

If you want to understand the detail of build steps, please read "Cross compiling" section of [README_cmake](../../README_cmake.md#cross-compiling), and then `build.sh` and `CMakeLists.txt` in each sub directories.

### Build procedure
#### Baremetal target
As a prerequisite, toolchain described above needs to be installed in your host machine. The detail of the toolchanin configuration is set in [toolchain.arm-32-sample.cmake](cmake/toolchain.arm-32-sample.cmake), which you might need to modify depending on the target baremetal system.
Then, just run the build script in one of the aforementioned sub directories.
```
cd cmake-<xxx>/
./build.sh
```

#### Host target
This application can be built for host target as well by the simple steps shown below.
```
cd cmake-<xxx>/
cmake -DCMAKE_PREFIX_PATH=<path/to/halide install> -B build .
cmake --build build/
```

## How to run
`run.sh` in each sub directories is a sample script that shows how to run the executable on QEMU Arm System emulator with semihosting mode.
