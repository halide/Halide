# Halide

Halide is a programming language designed to make it easier to write
high-performance image and array processing code on modern machines. Halide
currently targets:

- CPU architectures: X86, ARM, Hexagon, PowerPC, RISC-V
- Operating systems: Linux, Windows, macOS, Android, iOS, Qualcomm QuRT
- GPU Compute APIs: CUDA, OpenCL, Apple Metal, Microsoft Direct X 12, Vulkan

Rather than being a standalone programming language, Halide is embedded in C++.
This means you write C++ code that builds an in-memory representation of a
Halide pipeline using Halide's C++ API. You can then compile this representation
to an object file, or JIT-compile it and run it in the same process. Halide also
provides a Python binding that provides full support for writing Halide embedded
in Python without C++.

Halide requires C++17 (or later) to use.

For more detail about what Halide is, see https://halide-lang.org.

For API documentation see https://halide-lang.org/docs.

For some example code, read through the tutorials online
at https://halide-lang.org/tutorials. The corresponding code is in the
`tutorials/` directory. Larger examples are in the `apps/` directory.

If you've acquired a full source distribution and want to build Halide, see the
[notes below](#building-halide).

# Getting Halide

## Pip

As of Halide 19.0.0, we provide binary wheels on PyPI. Halide provides bindings
for C++ and Python. Even if you only intend to use Halide from C++, pip may be
the easiest way to get a binary build of Halide.

Full releases may be installed with `pip` like so:

```shell
$ pip install halide
```

Every commit to `main` is published to Test PyPI as a development version and
these may be installed with a few extra flags:

```shell
$ pip install halide --pre --extra-index-url https://test.pypi.org/simple
```

Currently, we provide wheels for: Windows x86-64, macOS x86-64, macOS arm64, and
Linux x86-64. The Linux wheels are built for manylinux_2_28, which makes them
broadly compatible (Debian 10, Ubuntu 18.10, Fedora 29).

*For C++ usage of the pip package:* On Linux and macOS, CMake's `find_package`
command should find Halide as long as you're in the same virtual environment you
installed it in. On Windows, you will need to add the virtual environment root
directory to `CMAKE_PREFIX_PATH`. This can be done by running
`set CMAKE_PREFIX_PATH=%VIRTUAL_ENV%` in `cmd`.

Other build systems can find the Halide root path by running `python -c 
"import halide; print(halide.install_dir())"`.

## Homebrew

Alternatively, if you use macOS, you can install Halide via
[Homebrew](https://brew.sh/) like so:

```
$ brew install halide
```

## Binary tarballs

The latest version of Halide can always be found on GitHub
at https://github.com/halide/Halide/releases

We provide binary releases for many popular platforms and architectures,
including 32/64-bit x86 Windows, 64-bit x86/ARM macOS, and 32/64-bit x86/ARM
Ubuntu Linux.

The macOS releases are built using XCode's command-line tools with Apple Clang
500.2.76. This means that we link against libc++ instead of libstdc++. You may
need to adjust compiler options accordingly if you're using an older XCode which
does not default to libc++.

We use a recent Ubuntu LTS to build the Linux releases; if your distribution is
too old, it might not have the requisite glibc. 

Nightly builds of Halide and the LLVM versions we use in CI are also available
at https://buildbot.halide-lang.org/

## Vcpkg

If you use [vcpkg](https://github.com/microsoft/vcpkg) to manage dependencies,
you can install Halide via:

```
$ vcpkg install halide:x64-windows # or x64-linux/x64-osx
```

One caveat: vcpkg installs only the minimum Halide backends required to compile
code for the active platform. If you want to include all the backends, you
should install `halide[target-all]:x64-windows` instead. Note that since this
will build LLVM, it will take a _lot_ of disk space (up to 100GB).

## Other package managers

We are interested in bringing Halide to other popular package managers and Linux
distribution repositories! We track the status of various distributions of
Halide [in this GitHub issue](https://github.com/halide/Halide/issues/4660). If
you have experience publishing packages we would be happy to work with you!

# Building Halide

## Platform Support

There are two sets of platform requirements relevant to Halide: those required
to run the compiler library in either JIT or AOT mode, and those required to run
the _binary outputs_ of the AOT compiler.

These are the **tested** host toolchain and platform combinations for building
and running the Halide compiler library.

| Compiler   | Version      | OS                     | Architectures |
|------------|--------------|------------------------|---------------|
| GCC        | 9.5          | Ubuntu Linux 20.04 LTS | x86, x64      |
| GCC        | 11.4         | Ubuntu Linux 22.04 LTS | ARM32, ARM64  |
| MSVC       | 2022 (19.37) | Windows 11 (22631)     | x86, x64      |
| AppleClang | 15.0.0       | macOS 14.4.1           | x64           |
| AppleClang | 14.0.0       | macOS 14.6             | ARM64         |

Some users have successfully built Halide for Linux using Clang 9.0.0+, for
Windows using ClangCL 11.0.0+, and for Windows ARM64 by cross-compiling with
MSVC. We do not actively test these scenarios, however, so your mileage may
vary.

Beyond these, we are willing to support (by accepting PRs for) platform and
toolchain combinations that still receive _active, first-party, public support_
from their original vendors. For instance, at time of writing, this excludes
Windows 7 and includes Ubuntu 18.04 LTS.

Compiled AOT pipelines are expected to have much broader platform support. The
binaries use the C ABI, and we expect any compliant C compiler to be able to use
the generated headers correctly. The C++ bindings currently require C++17. If
you discover a compatibility problem with a generated pipeline, please open an
issue.

## Acquiring LLVM

At any point in time, building Halide requires either the latest stable version
of LLVM, the previous stable version of LLVM, or trunk. At the time of writing,
this means versions 19, 18, and 17 are supported, but 16 is not.

It is simplest to get a binary release of LLVM on macOS by using
[Homebrew](https://brew.sh). Just run `brew install llvm`. On Debian flavors of
Linux, the [LLVM APT repo](https://apt.llvm.org) is best; use the provided
installation script. We know of no suitable official binary releases for
Windows, however the ones we use in CI can usually be found at
https://buildbot.halide-lang.org, along with tarballs for our other tested
platforms. See [the section on Windows](#windows) below for further advice.

If your OS does not have packages for LLVM, or you want more control over the
configuration, you can build it yourself. First check it out from GitHub:

```shell
$ git clone --depth 1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git
```

(LLVM 18.1.8 is the most recent released LLVM at the time of writing. For
current trunk, use `main` instead)

Then build it like so:

```shell
$ cmake -G Ninja -S llvm-project/llvm -B build \
        -DCMAKE_BUILD_TYPE=Release
        -DLLVM_ENABLE_PROJECTS="clang;lld;clang-tools-extra" \
        -DLLVM_ENABLE_RUNTIMES=compiler-rt \
        -DLLVM_TARGETS_TO_BUILD="WebAssembly;X86;AArch64;ARM;Hexagon;NVPTX;PowerPC;RISCV" \
        -DLLVM_ENABLE_ASSERTIONS=ON \
        -DLLVM_ENABLE_EH=ON \
        -DLLVM_ENABLE_RTTI=ON \
        -DLLVM_ENABLE_HTTPLIB=OFF \
        -DLLVM_ENABLE_LIBEDIT=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_BUILD_32_BITS=OFF
$ cmake --build build
$ cmake --install build --prefix llvm-install
```

This will produce a working LLVM installation in `$PWD/llvm-install`. We refer
to this path as `LLVM_ROOT` later. **Do not confuse this installation tree with
the build tree!**

LLVM takes a long time to build, so the above command uses Ninja to maximize
parallelism. If you choose to omit `-G Ninja`, Makefiles will be generated
instead. In this case, enable parallelism with `cmake --build build -j NNN`
where `NNN` is the number of parallel jobs, i.e. the number of CPUs you have.

Note that you _must_ add `clang` and `lld` to `LLVM_ENABLE_PROJECTS` and
`WebAssembly` and `X86` _must_ be included in `LLVM_TARGETS_TO_BUILD`.
`LLVM_ENABLE_RUNTIMES=compiler-rt` is only required to build the fuzz tests, and
`clang-tools-extra` is only necessary if you plan to contribute code to Halide
(so that you can run `clang-tidy` on your pull requests). You can disable
exception handling (EH) and RTTI if you don't want the Python bindings. We
recommend enabling the full set to simplify builds during development.

## Building Halide with CMake

This is discussed in greater detail in [BuildingHalideWithCMake.md]. CMake
version 3.28+ is required to build Halide.

[BuildingHalideWithCMake.md]: doc/BuildingHalideWithCMake.md

### MacOS and Linux

Follow the above instructions to build LLVM or acquire a suitable binary
release. Then change directory to the Halide repository and run:

```shell
$ cmake -G Ninja  -S . -B build -DCMAKE_BUILD_TYPE=Release -DHalide_LLVM_ROOT=$LLVM_ROOT
$ cmake --build build
```

Setting `-DHalide_LLVM_ROOT` is not required if you have a suitable system-wide
version installed. However, if you have multiple LLVMs installed, it can pick
between them.

### Windows

We suggest building with Visual Studio 2022. Your mileage may vary with earlier
versions. Be sure to install the "C++ CMake tools for Windows" in the Visual
Studio installer. For older versions of Visual Studio, do not install the CMake
tools, but instead acquire CMake and Ninja from their respective project
websites.

These instructions start from the `D:` drive. We assume this git repo is cloned
to `D:\Halide`. We also assume that your shell environment is set up correctly.
For a 64-bit build, run:

```
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
```

For a 32-bit build, run:

```
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86
```

#### Managing dependencies with vcpkg

The best way to get compatible dependencies on Windows is to use
[vcpkg](https://github.com/Microsoft/vcpkg). Install it like so:

```
D:\> git clone https://github.com/Microsoft/vcpkg.git
D:\> cd vcpkg
D:\vcpkg> .\bootstrap-vcpkg.bat -disableMetrics
...
CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

When using the toolchain file, vcpkg will automatically build all the necessary
dependencies. However, as stated above, be aware that acquiring LLVM this way
may use over 100 GB of disk space for its build trees and take a very long time
to build. You can manually delete the build trees afterward, but vcpkg will not
do this automatically.

See [BuildingHalideWithCMake.md](./doc/BuildingHalideWithCMake.md#vcpkg-presets)
for directions to use Vcpkg for everything _except_ LLVM.

#### Building Halide

Create a separate build tree and call CMake with vcpkg's toolchain. This will
build in either 32-bit or 64-bit depending on the environment script (`vcvars`)
that was run earlier.

```
D:\Halide> cmake -G Ninja -S . -B build ^
                 --toolchain D:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
                 -DCMAKE_BUILD_TYPE=Release
```

Then run the build with:

```
D:\Halide> cmake --build build
```

To run all the tests:

```
D:\Halide> ctest --test-dir build --output-on-failure
```

Subsets of the tests can be selected with `-L` and include `correctness`,
`generator`, `error`, and the other directory names under `tests/`.

#### Building LLVM (optional)

Follow these steps if you want to build LLVM yourself. First, download LLVM's
sources (these instructions use the 18.1.8 release).

```
D:\> git clone --depth 1 --branch llvm-org-18.1.8 https://github.com/llvm/llvm-project.git
```

As above, run `vcvarsall.bat` to pick between x86 and x64. Then configure LLVM
with the following command (for 32-bit, set `-DLLVM_BUILD_32_BITS=ON` instead):

```
D:\> cmake -G Ninja -S llvm-project\llvm -B build ^
           -DCMAKE_BUILD_TYPE=Release ^
           -DLLVM_ENABLE_PROJECTS=clang;lld;clang-tools-extra ^
           -DLLVM_ENABLE_RUNTIMES=compiler-rt ^
           -DLLVM_TARGETS_TO_BUILD=WebAssembly;X86;AArch64;ARM;Hexagon;NVPTX;PowerPC;RISCV ^
           -DLLVM_ENABLE_ASSERTIONS=ON ^
           -DLLVM_ENABLE_EH=ON ^
           -DLLVM_ENABLE_RTTI=ON ^
           -DLLVM_ENABLE_HTTPLIB=OFF ^
           -DLLVM_ENABLE_LIBEDIT=OFF ^
           -DLLVM_ENABLE_LIBXML2=OFF ^
           -DLLVM_ENABLE_TERMINFO=OFF ^
           -DLLVM_ENABLE_ZLIB=OFF ^
           -DLLVM_ENABLE_ZSTD=OFF ^
           -DLLVM_BUILD_32_BITS=OFF
```

**MSBuild:** If you want to build LLVM with MSBuild instead of Ninja, use
`-G "Visual Studio 17 2022" -Thost=x64 -A x64` or
`-G "Visual Studio 17 2022" -Thost=x64 -A Win32` in place of `-G Ninja`.

Finally, run the build and install to a local directory:

```
D:\> cmake --build build --config Release
D:\> cmake --install build --prefix llvm-install
```

You can substitute `Debug` for `Release` in the above `cmake` commands if you
want a debug build.

To use this with Halide, but still allow vcpkg to manage other dependencies, you
must add two flags to Halide's CMake configure command line. First, disable LLVM
with `-DVCPKG_OVERLAY_PORTS=cmake/vcpkg`. Second, point CMake to our newly built
Halide with `-DHalide_LLVM_ROOT=D:/llvm-install`.

#### If all else fails...

Do what the buildbots do: https://buildbot.halide-lang.org/master/#/builders

If the row that best matches your system is red, then maybe things aren't just
broken for you. If it's green, then you can click through to the latest build
and see the commands that the build bots run. Open a step ("Configure Halide" is
useful) and look at the "stdio" logs in the viewer. These logs contain the full
commands that were run, as well as the environment variables they were run with.

## Building Halide with make

> [!WARNING]
> We do not provide support for the Makefile. Feel free to use it, but if
> anything goes wrong, switch to the CMake build. Note also that the Makefile
> cannot build the Python bindings or produce install packages.

*TL;DR*: Have LLVM 17 (or greater) installed and run `make` in the root
directory of the repository (where this README is).

By default, `make` will use the `llvm-config` tool found in the `PATH`. If you
want to use a different LLVM, such as a custom-built one following the
instructions above, set the following environment variable:

```shell
$ export LLVM_CONFIG="$LLVM_ROOT/bin/llvm-config"
```

Now you should be able to just run `make` in the root directory of the Halide
source tree. `make run_tests` will run the JIT test suite, and `make test_apps`
will make sure all the apps compile and run (but won't check their output).

When building the tests, you can set the AOT compilation target with the 
`HL_TARGET` environment variable.

### Building Halide out-of-tree with make

If you wish to build Halide in a separate directory, you can do that like so:

```shell
$ cd ..
$ mkdir halide_build
$ cd halide_build
$ make -f ../Halide/Makefile
```

# Some useful environment variables

`HL_JIT_TARGET=...` will set Halide's JIT compilation target.

`HL_DEBUG_CODEGEN=1` will print out pseudocode for what Halide is compiling.
Higher numbers will print more detail.

`HL_NUM_THREADS=...` specifies the number of threads to create for the thread
pool. When the async scheduling directive is used, more threads than this number
may be required and thus allocated. A maximum of 256 threads is allowed. (By
default, the number of cores on the host is used.)

`HL_TRACE_FILE=...` specifies a binary target file to dump tracing data into
(ignored unless at least one `trace_` feature is enabled in the target). The
output can be parsed programmatically by starting from the code in
`utils/HalideTraceViz.cpp`.

# Further references

We have more documentation in `doc/`, the following links might be helpful:

| Document                                      | Description                                                               |
|-----------------------------------------------|---------------------------------------------------------------------------|
| [CMake build](doc/BuildingHalideWithCMake.md) | How to configure and build Halide using CMake.                            |
| [CMake package](doc/HalideCMakePackage.md)    | How to use the Halide CMake package to build your code.                   |
| [Hexagon](doc/Hexagon.md)                     | How to use the Hexagon backend.                                           |
| [Python](doc/Python.md)                       | Documentation for the Python bindings.                                    |
| [RunGen](doc/RunGen.md)                       | How to use the RunGen interface to run and benchmark arbitrary pipelines. |
| [Vulkan](doc/Vulkan.md)                       | How to use the Halide Vulkan backend (BETA)                               |
| [WebAssembly](doc/WebAssembly.md)             | How to use the WebAssembly backend and how to use V8 in place of wabt.    |
| [WebGPU](doc/WebGPU.md)                       | How to run WebGPU pipelines (BETA)                                        |

The following links are of greater interest to developers wishing to contribute
code to Halide:

| Document                                 | Description                                                                                                   |
|------------------------------------------|---------------------------------------------------------------------------------------------------------------|
| [CMake developer](doc/CodeStyleCMake.md) | Guidelines for authoring new CMake code.                                                                      |
| [FuzzTesting](doc/FuzzTesting.md)        | Information about fuzz testing the Halide compiler (rather than pipelines). Intended for internal developers. |
