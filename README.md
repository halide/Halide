# Halide

Halide is a programming language designed to make it easier to write
high-performance image and array processing code on modern machines. Halide
currently targets:

- CPU architectures: X86, ARM, MIPS, Hexagon, PowerPC
- Operating systems: Linux, Windows, Mac OS X, Android, iOS, Qualcomm QuRT
- GPU Compute APIs: CUDA, OpenCL, OpenGL, OpenGL Compute Shaders, Apple Metal,
  Microsoft Direct X 12

Rather than being a standalone programming language, Halide is embedded in C++.
This means you write C++ code that builds an in-memory representation of a
Halide pipeline using Halide's C++ API. You can then compile this representation
to an object file, or JIT-compile it and run it in the same process. Halide also
provides a Python binding that provides full support for writing Halide embedded
in Python without C++.

For more detail about what Halide is, see http://halide-lang.org.

For API documentation see http://halide-lang.org/docs

To see some example code, look in the tutorials directory.

If you've acquired a full source distribution and want to build Halide, see the
[notes below](#building-halide-with-cmake).

# Getting Halide

## Binary tarballs

The latest version of Halide is **Halide 10.0.0**. We provide binary releases
for many popular platforms and architectures, including 32/64-bit x86 Windows,
64-bit macOS, and 32/64-bit x86/ARM Ubuntu Linux. See the releases tab on the
right (or click [here](https://github.com/halide/Halide/releases/tag/v10.0.0)).

## Vcpkg

If you use [vcpkg](https://github.com/microsoft/vcpkg) to manage dependencies,
you can install Halide via:

```
$ vcpkg install halide:x64-windows # or x64-linux/x64-osx
```

Note two caveats: first, at time of writing,
[MSVC mis-compiles LLVM](https://github.com/halide/Halide/issues/5039) on
x86-windows, so Halide cannot be used in vcpkg on that platform at this time;
second, vcpkg installs only the minimum Halide backends required to compile code
for the active platform. If you want to include all the backends, you should
install `halide[target-all]:x64-windows` instead. Note that since this will
build LLVM, it will take a _lot_ of disk space (up to 100GB).

## Homebrew

Alternatively, if you use macOS, you can install Halide via
[Homebrew](https://brew.sh/) like so:

```
$ brew install halide
```

## Other package managers

We are interested in bringing Halide 10 to other popular package managers
and Linux distribution repositories including, but not limited to, Conan,
Debian, [Ubuntu (or PPA)](https://github.com/halide/Halide/issues/5285),
CentOS/Fedora, and Arch. If you have experience publishing packages we
would be happy to work with you!

If you are a maintainer of any other package distribution platform, we would
be excited to work with you, too.

# Building Halide with Make

### TL;DR

Have llvm-9.0 (or greater) installed and run `make` in the root directory of the
repository (where this README is).

### Acquiring LLVM

At any point in time, building Halide requires either the latest stable version
of LLVM, the previous stable version of LLVM, and trunk. At the time of writing,
this means versions 11.0 and 10.0 are supported, but 9.0 is not. The commands
`llvm-config` and `clang` must be somewhere in the path.

If your OS does not have packages for llvm, you can find binaries for it at
http://llvm.org/releases/download.html. Download an appropriate package and then
either install it, or at least put the `bin` subdirectory in your path. (This
works well on OS X and Ubuntu.)

If you want to build it yourself, first check it out from GitHub:

```
% git clone --depth 1 --branch llvmorg-11.0.0 https://github.com/llvm/llvm-project.git
```

(If you want to build LLVM 10.x, use branch `llvmorg-10.0.1`; for current trunk,
use `master`)

Then build it like so:

```
% cmake -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_ENABLE_PROJECTS="clang;lld;clang-tools-extra" \
        -DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX;AArch64;Mips;Hexagon" \
        -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_ASSERTIONS=ON \
        -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_BUILD_32_BITS=OFF \
        -S llvm-project/llvm -B llvm-build
% cmake --build llvm-build
% cmake --install llvm-build --prefix llvm-install
```

then to point Halide to it:

```
% export LLVM_ROOT=$PWD/llvm-install
% export LLVM_CONFIG=$LLVM_ROOT/bin/llvm-config
```

Note that you _must_ add `clang` to `LLVM_ENABLE_PROJECTS`; adding `lld` to
`LLVM_ENABLE_PROJECTS` is only required when using WebAssembly, and adding
`clang-tools-extra` is only necessary if you plan to contribute code to Halide
(so that you can run clang-tidy on your pull requests). We recommend enabling
both in all cases, to simplify builds. You can disable exception handling (EH)
and RTTI if you don't want the Python bindings.

### Building Halide with make

With `LLVM_CONFIG` set (or `llvm-config` in your path), you should be able to
just run `make` in the root directory of the Halide source tree.
`make run_tests` will run the JIT test suite, and `make test_apps` will make
sure all the apps compile and run (but won't check their output).

There is no `make install` yet. If you want to make an install package, run
`make distrib`.

### Building Halide out-of-tree with make

If you wish to build Halide in a separate directory, you can do that like so:

    % cd ..
    % mkdir halide_build
    % cd halide_build
    % make -f ../Halide/Makefile

# Building Halide with CMake

### MacOS and Linux

Follow the above instructions to build LLVM or acquire a suitable binary
release. Then change directory to the Halide repository and run:

```
% cmake -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=$LLVM_ROOT/lib/cmake/llvm -S . -B build
% cmake --build build
```

`LLVM_DIR` is the folder in the LLVM installation tree **(do not use the build
tree by mistake)** that contains `LLVMConfig.cmake`. It is not required to set
this variable if you have a suitable system-wide version installed. If you have
multiple system-wide versions installed, you can specify the version with
`Halide_REQUIRE_LLVM_VERSION`. Add `-G Ninja` if you prefer to build with the
Ninja generator.

### Windows

We suggest building with Visual Studio 2019. Your mileage may vary with earlier
versions. Be sure to install the "C++ CMake tools for Windows" in the Visual
Studio installer. For older versions of Visual Studio, do not install the CMake
tools, but instead acquire CMake and Ninja from their respective project
websites.

These instructions start from the `D:` drive. We assume this git repo is cloned
to `D:\Halide`. We also assume that your shell environment is set up correctly.
For a 64-bit build, run:

```
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
```

For a 32-bit build, run:

```
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86
```

#### Managing dependencies with vcpkg

The best way to get compatible dependencies on Windows is to use
[vcpkg](https://github.com/Microsoft/vcpkg). Install it like so:

```
D:\> git clone https://github.com/Microsoft/vcpkg.git
D:\> cd vcpkg
D:\> .\bootstrap-vcpkg.bat
D:\vcpkg> .\vcpkg integrate install
...
CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

Then install the libraries. For a 64-bit build, run:

```
D:\vcpkg> .\vcpkg install libpng:x64-windows libjpeg-turbo:x64-windows llvm[target-all,clang-tools-extra]:x64-windows
```

To support 32-bit builds, also run:

```
D:\vcpkg> .\vcpkg install libpng:x86-windows libjpeg-turbo:x86-windows llvm[target-all,clang-tools-extra]:x86-windows
```

#### Building Halide

Create a separate build tree and call CMake with vcpkg's toolchain. This will
build in either 32-bit or 64-bit depending on the environment script (`vcvars`)
that was run earlier.

```
D:\Halide> cmake -G Ninja ^
                 -DCMAKE_BUILD_TYPE=Release ^
                 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
                 -S . -B build
```

**Note:** If building with Python bindings on 32-bit (enabled by default), be
sure to point CMake to the installation path of a 32-bit Python 3. You can do
this by specifying, for example:
`"-DPython3_ROOT_DIR=C:\Program Files (x86)\Python38-32"`.

Then run the build with:

```
D:\Halide> cmake --build build --config Release -j %NUMBER_OF_PROCESSORS%
```

To run all the tests:

```
D:\Halide> cd build
D:\Halide\build> ctest -C Release
```

Subsets of the tests can be selected with `-L` and include `correctness`,
`python`, `error`, and the other directory names under `/tests`.

#### Building LLVM (optional)

Follow these steps if you want to build LLVM yourself. First, download LLVM's
sources (these instructions use the latest 11.0 release)

```
D:\> git clone --depth 1 --branch llvmorg-11.0.0 https://github.com/llvm/llvm-project.git
```

For a 64-bit build, run:

```
D:\> cmake -G Ninja ^
           -DCMAKE_BUILD_TYPE=Release ^
           -DLLVM_ENABLE_PROJECTS=clang;lld;clang-tools-extra ^
           -DLLVM_ENABLE_TERMINFO=OFF ^
           -DLLVM_TARGETS_TO_BUILD=X86;ARM;NVPTX;AArch64;Mips;Hexagon ^
           -DLLVM_ENABLE_ASSERTIONS=ON ^
           -DLLVM_ENABLE_EH=ON ^
           -DLLVM_ENABLE_RTTI=ON ^
           -DLLVM_BUILD_32_BITS=OFF ^
           -S llvm-project\llvm -B llvm-build
```

For a 32-bit build, run:

```
D:\> cmake -G Ninja ^
           -DCMAKE_BUILD_TYPE=Release ^
           -DLLVM_ENABLE_PROJECTS=clang;lld;clang-tools-extra ^
           -DLLVM_ENABLE_TERMINFO=OFF ^
           -DLLVM_TARGETS_TO_BUILD=X86;ARM;NVPTX;AArch64;Mips;Hexagon ^
           -DLLVM_ENABLE_ASSERTIONS=ON ^
           -DLLVM_ENABLE_EH=ON ^
           -DLLVM_ENABLE_RTTI=ON ^
           -DLLVM_BUILD_32_BITS=ON ^
           -S llvm-project\llvm -B llvm32-build
```

Finally, run:

```
D:\> cmake --build llvm-build --config Release -j %NUMBER_OF_PROCESSORS%
D:\> cmake --install llvm-build --prefix llvm-install
```

You can substitute `Debug` for `Release` in the above `cmake` commands if you
want a debug build. Make sure to add `-DLLVM_DIR=D:/llvm-install/lib/cmake/llvm`
to the Halide CMake command to override `vcpkg`'s LLVM.

**MSBuild:** If you want to build LLVM with MSBuild instead of Ninja, use
`-G "Visual Studio 16 2019" -Thost=x64 -A x64` or
`-G "Visual Studio 16 2019" -Thost=x64 -A Win32` in place of `-G Ninja`.

#### If all else fails...

Do what the build-bots do: https://buildbot.halide-lang.org/master/#/builders

If the column that best matches your system is red, then maybe things aren't
just broken for you. If it's green, then you can click the "stdio" links in the
latest build to see what commands the build bots run, and what the output was.

# Some useful environment variables

`HL_TARGET=...` will set Halide's AOT compilation target.

`HL_JIT_TARGET=...` will set Halide's JIT compilation target.

`HL_DEBUG_CODEGEN=1` will print out pseudocode for what Halide is compiling.
Higher numbers will print more detail.

`HL_NUM_THREADS=...` specifies the number of threads to create for the thread
pool. When the async scheduling directive is used, more threads than this number
may be required and thus allocated. A maximum of 256 threads is allowed. (By
default, the number of cores on the host is used.)

`HL_TRACE_FILE=...` specifies a binary target file to dump tracing data into
(ignored unless at least one `trace_` feature is enabled in `HL_TARGET` or
`HL_JIT_TARGET`). The output can be parsed programmatically by starting from the
code in `utils/HalideTraceViz.cpp`.

# Using Halide on OSX

Precompiled Halide distributions are built using XCode's command-line tools with
Apple clang 500.2.76. This means that we link against libc++ instead of
libstdc++. You may need to adjust compiler options accordingly if you're using
an older XCode which does not default to libc++.

# Halide OpenGL/GLSL backend

Halide's OpenGL backend offloads image processing operations to the GPU by
generating GLSL-based fragment shaders.

Compared to other GPU-based processing options such as CUDA and OpenCL, OpenGL
has two main advantages: it is available on basically every desktop computer and
mobile device, and it is generally well supported across different hardware
vendors.

The main disadvantage of OpenGL as an image processing framework is that the
computational capabilities of fragment shaders are quite restricted. In general,
the processing model provided by OpenGL is most suitable for filters where each
output pixel can be expressed as a simple function of the input pixels. This
covers a wide range of interesting operations like point-wise filters and
convolutions; but a few common image processing operations such as histograms or
recursive filters are notoriously hard to express in GLSL.

#### Writing OpenGL-Based Filters

To enable code generation for OpenGL, include `opengl` in the target specifier
passed to Halide. Since OpenGL shaders are limited in their computational power,
you must also specify a CPU target for those parts of the filter that cannot or
should not be computed on the GPU. Examples of valid target specifiers are

```
host-opengl
x86-opengl-debug
```

Adding `debug`, as in the second example, adds additional logging output and is
highly recommended during development.

By default, filters compiled for OpenGL targets run completely on the CPU.
Execution on the GPU must be enabled for individual Funcs by appropriate
scheduling calls.

GLSL fragment shaders implicitly iterate over two spatial dimensions x,y and the
color channel. Due to the way color channels handled in GLSL, only filters for
which the color index is a compile-time constant can be scheduled. The main
consequence is that the range of color variables must be explicitly specified
for both input and output buffers before scheduling:

```
ImageParam input;
Func f;
Var x, y, c;
f(x, y, c) = ...;

input.set_bounds(2, 0, 3);   // specify color range for input
f.bound(c, 0, 3);            // and output
f.glsl(x, y, c);
```

#### JIT Compilation

For JIT compilation Halide attempts to load the system libraries for opengl and
creates a new context to use for each module. Windows is not yet supported.

Examples for JIT execution of OpenGL-based filters can be found in test/opengl.

#### AOT Compilation

When AOT (ahead-of-time) compilation is used, Halide generates OpenGL-enabled
object files that can be linked to and called from a host application. In
general, this is fairly straightforward, but a few things must be taken care of.

On Linux, OS X, and Android, Halide creates its own OpenGL context unless the
current thread already has an active context. On other platforms you have to
link implementations of the following two functions with your Halide code:

```
extern "C" int halide_opengl_create_context(void *) {
    return 0;  // if successful
}

extern "C" void *halide_opengl_get_proc_addr(void *, const char *name) {
    ...
}
```

Halide allocates and deletes textures as necessary. Applications may manage the
textures by hand by setting the `halide_buffer_t::device` field; this is most
useful for reusing image data that is already stored in textures. Some
rudimentary checks are performed to ensure that externally allocated textures
have the correct format, but in general that's the responsibility of the
application.

It is possible to let render directly to the current framebuffer; to do this,
set the `dev` field of the output buffer to the value returned by
`halide_opengl_output_client_bound`. The example in apps/HelloAndroidGL
demonstrates this technique.

Some operating systems can delete the OpenGL context of suspended applications.
If this happens, Halide needs to re-initialize itself with the new context after
the application resumes. Call `halide_opengl_context_lost` to reset Halide's
OpenGL state after this has happened.

#### Limitations

The current implementation of the OpenGL backend targets the common subset of
OpenGL 2.0 and OpenGL ES 2.0 which is widely available on both mobile devices
and traditional computers. As a consequence, only a subset of the Halide
language can be scheduled to run using OpenGL. Some important limitations are:

- Reductions cannot be implemented in GLSL and must be run on the CPU.

- OpenGL ES 2.0 only supports uint8 buffers.

  Support for floating point texture is available, but requires OpenGL (ES) 3.0
  or the texture_float extension, which may not work on all mobile devices.

- OpenGL ES 2.0 has very limited support for integer arithmetic. For maximum
  compatibility, consider doing all computations using floating point, even when
  using integer textures.

- Only 2D images with 3 or 4 color channels can be scheduled. Images with one or
  two channels require OpenGL (ES) 3.0 or the texture_rg extension.

- Not all builtin functions provided by Halide are currently supported, for
  example `fast_log`, `fast_exp`, `fast_pow`, `reinterpret`, bit operations,
  `random_float`, `random_int` cannot be used in GLSL code.

The maximum texture size in OpenGL is `GL_MAX_TEXTURE_SIZE`, which is often
smaller than the image of interest; on mobile devices, for example,
`GL_MAX_TEXTURE_SIZE` is commonly 2048. Tiling must be used to process larger
images.

Planned features:

- Support for half-float textures and arithmetic

- Support for integer textures and arithmetic

(Note that OpenGL Compute Shaders are supported with a separate OpenGLCompute
backend.)

# Halide for Hexagon HVX

Halide supports offloading work to Qualcomm Hexagon DSP on Qualcomm Snapdragon
835 devices or newer. The Hexagon DSP provides a set of 128 byte vector instruction
extensions - the Hexagon Vector eXtensions (HVX). HVX is well suited for image
processing, and Halide for Hexagon HVX will generate the appropriate HVX vector
instructions from a program authored in Halide.

Halide can be used to compile Hexagon object files directly, by using a target
such as `hexagon-32-qurt-hvx`.

Halide can also be used to offload parts of a pipeline to Hexagon using the
`hexagon` scheduling directive. To enable the `hexagon` scheduling directive,
include the `hvx` target feature in your target. The currently
supported combination of targets is to use the HVX target features with an x86
linux host (to use the simulator) or with an ARM android target (to use Hexagon
DSP hardware). For examples of using the `hexagon` scheduling directive on both
the simulator and a Hexagon DSP, see the blur example app.

To build and run an example app using the Hexagon target,

1. Obtain and build trunk LLVM and Clang. (Earlier versions of LLVM may work but
   are not actively tested and thus not recommended.)
2. Download and install the Hexagon SDK and Hexagon Tools. Hexagon SDK 3.4.1 or later
   is needed. Hexagon Tools 8.2 or later is needed.
3. Build and run an example for Hexagon HVX

### 1. Obtain and build trunk LLVM and Clang

(Instructions given previous, just be sure to check out the `master` branch.)

### 2. Download and install the Hexagon SDK and Hexagon Tools

Go to https://developer.qualcomm.com/software/hexagon-dsp-sdk/tools

1. Select the Hexagon Series 600 Software and download the 3.4.1 version or later
   for Linux.
2. untar the installer
3. Run the extracted installer to install the Hexagon SDK and Hexagon Tools,
   selecting Installation of Hexagon SDK into `/location/of/SDK/Hexagon_SDK/3.x`
   and the Hexagon tools into `/location/of/SDK/Hexagon_Tools/8.x`
4. Set an environment variable to point to the SDK installation location
   ```
   export SDK_LOC=/location/of/SDK
   ```

### 3. Build and run an example for Hexagon HVX

In addition to running Hexagon code on device, Halide also supports running
Hexagon code on the simulator from the Hexagon tools.

To build and run the blur example in Halide/apps/blur on the simulator:

```
cd apps/blur
export HL_HEXAGON_SIM_REMOTE=../../src/runtime/hexagon_remote/bin/v62/hexagon_sim_remote
export HL_HEXAGON_TOOLS=$SDK_LOC/Hexagon_Tools/8.x/Tools/
LD_LIBRARY_PATH=../../src/runtime/hexagon_remote/bin/host/:$HL_HEXAGON_TOOLS/lib/iss/:. HL_TARGET=host-hvx make test
```

### To build and run the blur example in Halide/apps/blur on Android:

To build the example for Android, first ensure that you have Android NDK r19b or
later installed, and the ANDROID_NDK_ROOT environment variable points to it.
(Note that Qualcomm Hexagon SDK v3.5.2 includes Android NDK r19c, which is fine.)

Now build and run the blur example using the script to run it on device:

```
export HL_HEXAGON_TOOLS=$SDK_LOC/HEXAGON_Tools/8.0/Tools/
HL_TARGET=arm-64-android-hvx ./adb_run_on_device.sh
```
