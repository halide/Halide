# HelloBaremetal

## Overview

This is a sample application which demonstrates how to cross-compile the
application with Halide generated function for baremetal target system without
OS.

## Setup

There are wide varieties of baremetal system and this application is created
just for one of them. More specifically, it is tested with the following
conditions.

- Compiled by
  [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  (version 12.2) AArch32 bare-metal target (`arm-none-eabi`)
- Targeted for Arm 32 bit CPU with NEON (Cortex-A9) on Arm Realview boards on
  [QEMU Arm System emulator](https://www.qemu.org/docs/master/system/target-arm.html)
  (version 7.2.50)
- With "semihosting" mode, where some limited I/O communication is available
  between host and target (e.g. printf() to stdout)

Because we cannot rely on the abstraction of OS, you will probably need to
modify some parts if you target for different setup.

## How to build

Unlike most other apps in this repo, there is no top-level `CMakeLists.txt` here
-- each of the three `cmake-<xxx>` subdirectories below is an independent CMake
project you build directly.

### Cross compilation

Halide cross-compiling in CMake is tricky and there are a couple of ways to
realize it. This application demonstrates 3 different ways with concrete code,
located in `CMakeLists.txt` in the following subdirectories.

1. cmake-twice
2. cmake-super_build
3. cmake-external_project

If you want to understand the detail of build steps, please read "Cross
compiling" section of
[HalideCMakePackage](../../doc/HalideCMakePackage.md#cross-compiling), and then
`build.sh` and `CMakeLists.txt` in each subdirectory.

### Build procedure

#### Baremetal target

As a prerequisite, toolchain described above needs to be installed in your host
machine. The detail of the toolchain configuration is set in
[toolchain.noos-arm32-sample.cmake](cmake/toolchain.noos-arm32-sample.cmake),
which you might need to modify depending on the target baremetal system. Then,
just run the build script in one of the aforementioned subdirectories.

```
cd cmake-<xxx>/
./build.sh
```

#### Host target

This application can be built for host target as well by the simple steps shown
below.

```
cd cmake-<xxx>/
cmake -DCMAKE_PREFIX_PATH=<path/to/halide install> -B build .
cmake --build build/
```

## How to run

After building for the bare-metal target, `ctest` runs the executable on QEMU
Arm System emulator with semihosting mode:

```
ctest --test-dir build-target --output-on-failure  # cmake-twice
ctest --test-dir build/app --output-on-failure      # cmake-super_build
ctest --test-dir build --output-on-failure          # cmake-external_project
```

`run_baremetal.sh` (the script `ctest` invokes under the hood) can also be run
directly against any executable built for this target.
