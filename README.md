# Halide

Halide is a programming language designed to make it easier to write
high-performance image and array processing code on modern machines. Halide
currently targets:
  * CPU architectures: X86, ARM, MIPS, Hexagon, PowerPC
  * Operating systems: Linux, Windows, Mac OS X, Android, iOS, Qualcomm QuRT
  * GPU Compute APIs: CUDA, OpenCL, OpenGL, OpenGL Compute Shaders, Apple Metal, Microsoft Direct X 12

Rather than being a standalone programming language, Halide is
embedded in C++. This means you write C++ code that builds an
in-memory representation of a Halide pipeline using Halide's C++
API. You can then compile this representation to an object file, or
JIT-compile it and run it in the same process. Halide also provides a Python binding that provides full support for writing Halide embedded in Python without C++.

For more detail about what Halide is, see http://halide-lang.org.

For API documentation see http://halide-lang.org/docs

To see some example code, look in the tutorials directory.

If you've acquired a full source distribution and want to build
Halide, see the notes below.

Build Status
============

| Linux                        |
|------------------------------|
| [![linux build status][1]][2]|

[1]: https://travis-ci.org/halide/Halide.svg?branch=master
[2]: https://travis-ci.org/halide/Halide

Building Halide
===============

#### TL;DR

Have llvm-8.0 (or greater) installed and run `make` in the root
directory of the repository (where this README is).

#### Acquiring LLVM

Building Halide requires at least LLVM 8.0, along with the matching
version of Clang; we recommend using the most recent stable version of LLVM for
most users (LLVM 9.0 at the time of this writing). `llvm-config` and `clang`
must be somewhere in the path. If your OS does not have packages for llvm-8.0
(or newer), you can find binaries for it at http://llvm.org/releases/download.html.
Download an appropriate package and then either install it, or at least put the
`bin` subdirectory in your path. (This works well on OS X and Ubuntu.)

If you want to build it yourself, first check it out from GitHub:

    % git clone https://github.com/llvm/llvm-project.git
    % git checkout release/9.x  # to build LLVM 9.x

(If you want to build LLVM 9.x, use `git checkout release/9.x`; for LLVM 8.0, use `release 8.x`; for current trunk, use `git checkout master`)

Then build it like so:

    % cd llvm-project
    % mkdir build
    % cd build
    % cmake -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX;AArch64;Mips;PowerPC" -DLLVM_ENABLE_ASSERTIONS=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_32_BITS=OFF ../llvm
    % make -j

then to point Halide to it:

    export LLVM_CONFIG=<path to llvm>/build/bin/llvm-config

(Note that you *must* add `clang` to `LLVM_ENABLE_PROJECTS`; adding `lld` to `LLVM_ENABLE_PROJECTS` is only required when using WebAssembly, but we recommend enabling it in all cases, to simplify builds.)

#### Building Halide with make

With `LLVM_CONFIG` set (or `llvm-config` in your path), you should be
able to just run `make` in the root directory of the Halide source tree.
`make run_tests` will run the JIT test suite, and `make test_apps` will
make sure all the apps compile and run (but won't check their output).

There is no `make install` yet. If you want to make an install
package, run `make distrib`.

#### Building Halide out-of-tree with make

If you wish to build Halide in a separate directory, you can do that
like so:

    % cd ..
    % mkdir halide_build
    % cd halide_build
    % make -f ../Halide/Makefile

#### Building Halide with cmake

If you wish to use cmake to build Halide, the build procedure is:

    % mkdir cmake_build
    % cd cmake_build
    % cmake -DLLVM_DIR=/path-to-llvm-build/lib/cmake/llvm -DCMAKE_BUILD_TYPE=Release /path/to/halide
    % make -j

`LLVM_DIR` should be the folder in the LLVM installation or build tree that contains `LLVMConfig.cmake`.

#### Building Halide and LLVM on Windows

Acquire MSVC 2015 Update 3 or newer. Earlier versions may work but are
not part of our tests. MSBuild and cmake should also be in your
path. The instructions below assume Halide is checked out under
`C:\Code\Halide`, and LLVM and Clang are checked out under
`C:\Code\llvm`.

    % mkdir C:\Code\llvm-build
    % cd C:\Code\llvm-build
    % cmake -DCMAKE_INSTALL_PREFIX=../llvm-install -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_TARGETS_TO_BUILD=X86;ARM;NVPTX;AArch64;Mips;Hexagon -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_BUILD_32_BITS=OFF -DCMAKE_BUILD_TYPE=Release ../llvm/llvm -G "Visual Studio 14 Win64"

For a 32-bit build use:

    % cmake -DCMAKE_INSTALL_PREFIX=../llvm-install -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_TARGETS_TO_BUILD=X86;ARM;NVPTX;AArch64;Mips;Hexagon -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_BUILD_32_BITS=ON -DCMAKE_BUILD_TYPE=Release ../llvm/llvm -G "Visual Studio 14"

Then build it like so:

    % MSBuild.exe /m /t:Build /p:Configuration=Release .\INSTALL.vcxproj

You can substitute `Debug` for `Release` in both commands if you want a debug build.

To configure and build Halide:

    % mkdir C:\Code\halide-build
    % cd C:\Code\halide-build
    % cmake -DLLVM_DIR=../llvm-install/lib/cmake/llvm -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 14 Win64" ../halide
    % MSBuild.exe /m /t:Build /p:Configuration=Release .\ALL_BUILD.vcxproj

#### Building Halide and LLVM on Windows using mingw

The makefile method above should work from inside a "mingw64" shell
(not the default shell) in an msys2 installation.

#### If all else fails...

Do what the build-bots do: https://buildbot.halide-lang.org/master/#/builders

If the column that best matches your system is red, then maybe things
aren't just broken for you. If it's green, then you can click the
"stdio" links in the latest build to see what commands the build bots
run, and what the output was.

Some useful environment variables
=================================

`HL_TARGET=...` will set Halide's AOT compilation target.

`HL_JIT_TARGET=...` will set Halide's JIT compilation target.

`HL_DEBUG_CODEGEN=1` will print out pseudocode for what Halide is
compiling. Higher numbers will print more detail.

`HL_NUM_THREADS=...` specifies the number of threads to create for the
thread pool. When the async scheduling directive is used, more threads
than this number may be required and thus allocated. A maximum of 256
threads is allowed. (By default, the number of cores on the host is
used.)

`HL_TRACE_FILE=...` specifies a binary target file to dump tracing data
into (ignored unless at least one `trace_` feature is enabled in `HL_TARGET` or
`HL_JIT_TARGET`). The output can be parsed programmatically by starting from the
code in `utils/HalideTraceViz.cpp`.


Using Halide on OSX
===================

Precompiled Halide distributions are built using XCode's command-line
tools with Apple clang 500.2.76. This means that we link against
libc++ instead of libstdc++. You may need to adjust compiler options
accordingly if you're using an older XCode which does not default to
libc++.


Halide OpenGL/GLSL backend
==========================

Halide's OpenGL backend offloads image processing operations to the GPU by
generating GLSL-based fragment shaders.

Compared to other GPU-based processing options such as CUDA and OpenCL, OpenGL
has two main advantages: it is available on basically every desktop computer
and mobile device, and it is generally well supported across different
hardware vendors.

The main disadvantage of OpenGL as an image processing framework is that the
computational capabilities of fragment shaders are quite restricted. In
general, the processing model provided by OpenGL is most suitable for filters
where each output pixel can be expressed as a simple function of the input
pixels. This covers a wide range of interesting operations like point-wise
filters and convolutions; but a few common image processing operations such as
histograms or recursive filters are notoriously hard to express in GLSL.


#### Writing OpenGL-Based Filters

To enable code generation for OpenGL, include `opengl` in the target specifier
passed to Halide. Since OpenGL shaders are limited in their computational
power, you must also specify a CPU target for those parts of the filter that
cannot or should not be computed on the GPU. Examples of valid target
specifiers are

    host-opengl
    x86-opengl-debug

Adding `debug`, as in the second example, adds additional logging output and
is highly recommended during development.

By default, filters compiled for OpenGL targets run completely on the CPU.
Execution on the GPU must be enabled for individual Funcs by appropriate
scheduling calls.

GLSL fragment shaders implicitly iterate over two spatial dimensions x,y and
the color channel. Due to the way color channels handled in GLSL, only filters
for which the color index is a compile-time constant can be scheduled.  The
main consequence is that the range of color variables must be explicitly
specified for both input and output buffers before scheduling:

    ImageParam input;
    Func f;
    Var x, y, c;
    f(x, y, c) = ...;

    input.set_bounds(2, 0, 3);   // specify color range for input
    f.bound(c, 0, 3);            // and output
    f.glsl(x, y, c);


#### JIT Compilation

For JIT compilation Halide attempts to load the system libraries for opengl
and creates a new context to use for each module. Windows is not yet supported.

Examples for JIT execution of OpenGL-based filters can be found in test/opengl.


#### AOT Compilation

When AOT (ahead-of-time) compilation is used, Halide generates OpenGL-enabled
object files that can be linked to and called from a host application. In
general, this is fairly straightforward, but a few things must be taken care
of.

On Linux, OS X, and Android, Halide creates its own OpenGL context unless the
current thread already has an active context.  On other platforms you have to
link implementations of the following two functions with your Halide code:

    extern "C" int halide_opengl_create_context(void *) {
        return 0;  // if successful
    }

    extern "C" void *halide_opengl_get_proc_addr(void *, const char *name) {
        ...
    }

Halide allocates and deletes textures as necessary.  Applications may manage
the textures by hand by setting the `buffer_t::dev` field; this is most useful
for reusing image data that is already stored in textures. Some rudimentary
checks are performed to ensure that externally allocated textures have the
correct format, but in general that's the responsibility of the application.

It is possible to let render directly to the current framebuffer; to do this,
set the `dev` field of the output buffer to the value returned by
`halide_opengl_output_client_bound`.  The example in apps/HelloAndroidGL
demonstrates this technique.

Some operating systems can delete the OpenGL context of suspended
applications. If this happens, Halide needs to re-initialize itself with the
new context after the application resumes. Call `halide_opengl_context_lost`
to reset Halide's OpenGL state after this has happened.


#### Limitations

The current implementation of the OpenGL backend targets the common subset of
OpenGL 2.0 and OpenGL ES 2.0 which is widely available on both mobile devices
and traditional computers.  As a consequence, only a subset of the Halide
language can be scheduled to run using OpenGL. Some important limitations are:

  * Reductions cannot be implemented in GLSL and must be run on the CPU.

  * OpenGL ES 2.0 only supports uint8 buffers.

    Support for floating point texture is available, but requires OpenGL (ES)
    3.0 or the texture_float extension, which may not work on all mobile
    devices.

  * OpenGL ES 2.0 has very limited support for integer arithmetic. For maximum
    compatibility, consider doing all computations using floating point, even
    when using integer textures.

  * Only 2D images with 3 or 4 color channels can be scheduled. Images with
    one or two channels require OpenGL (ES) 3.0 or the texture_rg extension.

  * Not all builtin functions provided by Halide are currently supported, for
    example `fast_log`, `fast_exp`, `fast_pow`, `reinterpret`, bit operations,
    `random_float`, `random_int` cannot be used in GLSL code.

The maximum texture size in OpenGL is `GL_MAX_TEXTURE_SIZE`, which is often
smaller than the image of interest; on mobile devices, for example,
`GL_MAX_TEXTURE_SIZE` is commonly 2048. Tiling must be used to process larger
images.

Planned features:

  * Support for half-float textures and arithmetic

  * Support for integer textures and arithmetic

(Note that OpenGL Compute Shaders are supported with a separate
OpenGLCompute backend.)

Halide for Hexagon HVX
======================
Halide supports offloading work to Qualcomm Hexagon DSP on Qualcomm Snapdragon 820
devices or newer. The Hexagon DSP provides a set of 64 and 128 byte vector
instructions - the Hexagon Vector eXtensions (HVX). HVX is well suited to image
processing, and Halide for Hexagon HVX will generate the appropriate HVX vector
instructions from a program authored in Halide.

Halide can be used to compile Hexagon object files directly, by using a target such
as `hexagon-32-qurt-hvx_64` or `hexagon-32-qurt-hvx_128`.

Halide can also be used to offload parts of a pipeline to Hexagon using the `hexagon`
scheduling directive. To enable the `hexagon` scheduling directive, include the
`hvx_64` or `hvx_128` target features in your target. The currently supported
combination of targets is to use the HVX target features with an x86 linux
host (to use the simulator) or with an ARM android target (to use Hexagon DSP hardware).
For examples of using the `hexagon` scheduling directive on both the simulator and a
Hexagon DSP, see the blur example app.

To build and run an example app using the Hexagon target,
  1. Obtain and build trunk LLVM and Clang. (Earlier versions of LLVM may work but are not actively tested and thus not recommended.)
  2. Download and install the Hexagon SDK and version 8.0 Hexagon Tools
  3. Build and run an example for Hexagon HVX

#### 1. Obtain and build trunk LLVM and Clang

(Instructions given previous, just be sure to check out the `master` branch.)

#### 2. Download and install the Hexagon SDK and version 8.0 Hexagon Tools
Go to https://developer.qualcomm.com/software/hexagon-dsp-sdk/tools
  1. Select the Hexagon Series 600 Software and download the 3.0 version for Linux.
  2. untar the installer
  3. Run the extracted installer to install the Hexagon SDK and Hexagon Tools, selecting
  Installation of Hexagon SDK into `/location/of/SDK/Hexagon_SDK/3.0` and the Hexagon tools into `/location/of/SDK/Hexagon_Tools/8.0`
  4. Set an environment variable to point to the SDK installation location

    export SDK_LOC=/location/of/SDK

#### 3. Build and run an example for Hexagon HVX
In addition to running Hexagon code on device, Halide also supports running Hexagon
code on the simulator from the Hexagon tools.

To build and run the blur example in Halide/apps/blur on the simulator:

    cd apps/blur
    export HL_HEXAGON_SIM_REMOTE=../../src/runtime/hexagon_remote/bin/v60/hexagon_sim_remote
    export HL_HEXAGON_TOOLS=$SDK_LOC/Hexagon_Tools/8.0/Tools/
    LD_LIBRARY_PATH=../../src/runtime/hexagon_remote/bin/host/:$HL_HEXAGON_TOOLS/lib/iss/:. HL_TARGET=host-hvx_128 make test

#### To build and run the blur example in Halide/apps/blur on Android:

To build the example for Android, first ensure that you have a standalone toolchain
created from the NDK using the make-standalone-toolchain.sh script:

    export ANDROID_NDK_HOME=$SDK_LOC/Hexagon_SDK/3.0/tools/android-ndk-r10d/
    export ANDROID_ARM64_TOOLCHAIN=<path to put new arm64 toolchain>
    $ANDROID_NDK_HOME/build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN

Now build and run the blur example using the script to run it on device:

    export HL_HEXAGON_TOOLS=$SDK_LOC/HEXAGON_Tools/8.0/Tools/
    HL_TARGET=arm-64-android-hvx_128 ./adb_run_on_device.sh
