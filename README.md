Halide is a programming language designed to make it easier to write
high-performance image processing code on modern machines. Halide
currently targets X86, ARM, CUDA, OpenCL, and OpenGL on OS X, Linux,
and Windows.

Rather than being a standalone programming language, Halide is
embedded in C++. This means you write C++ code that builds an
in-memory representation of a Halide pipeline using Halide's C++
API. You can then compile this representation to an object file, or
JIT-compile it and run it in the same process.

For more detail about what Halide is, see http://halide-lang.org.

For API documentation see http://halide-lang.org/docs

To see some example code, look in the tutorials directory.

If you've acquired a full source distribution and want to build
Halide, see the notes below.


Some useful environment variables
=================================

HL_TARGET=... will set Halide's AOT compilation target.

HL_JIT_TARGET=... will set Halide's JIT compilation target.

HL_DEBUG_CODEGEN=1 will print out pseudocode for what Halide is
compiling. Higher numbers will print more detail.

HL_NUM_THREADS=... specifies the size of the thread pool. This has no
effect on OS X or iOS, where we just use grand central dispatch.

HL_TRACE=1 injects print statements into compiled Halide code that
will describe what the program is doing at runtime. Higher values
print more detail.

HL_TRACE_FILE=... specifies a binary target file to dump tracing data
into. The output can be parsed programmatically by starting from the
code in utils/HalideTrace.cpp

HL_PROFILE=1 injects timing data collection code. The output can be
parsed using utils/HalideProf.cpp


Using Halide on OSX
===================

Precompiled Halide distributions are built using XCode's command-line
tools with Apple clang 500.2.76. This means that we link against
libc++ instead of libstdc++. You may need to adjust compiler options
accordingly if you're using an older XCode which does not default to
libc++.

For parallelism, Halide automatically uses Apple's Grand Central
Dispatch, so it is not possible to control the number of threads used
without overriding the parallel runtime entirely.


Building Halide
===============

Building halide requires at least llvm 3.3, along with the matching
version of clang. llvm-config and clang must be somewhere in the
path. If your OS does not have packages for llvm-3.3, you can find
binaries for it at http://llvm.org/releases/download.html. Download an
appropriate package and then either install it, or at least put the
bin subdirectory in your path. (This works well on OS X and Ubuntu.)

If you want to build it yourself, first check it out from subversion:

    % svn co https://llvm.org/svn/llvm-project/llvm/branches/release_33 llvm3.3
    % svn co https://llvm.org/svn/llvm-project/cfe/branches/release_33 llvm3.3/tools/clang

Then build it like so:

    % cd llvm3.3
    % ./configure --disable-terminfo --enable-optimized --enable-assertions --with-clang --enable-targets=x86,arm,nvptx
    % make -j8

(Users of OSX 10.8+ may need to explicitly specify GCC vs Clang,
prepending "CC=gcc CXX=g++" to the configure command.)

Then finally tell Halide's Makefile about it like so:

    % export LLVM_CONFIG=<path to llvm>/Release+Asserts/bin/llvm-config
    % export CLANG=<path to llvm>/Release+Asserts/bin/clang

If you wish to use cmake to build llvm, the build procedure is:

    % cd llvm3.3
    % mkdir build
    % cd build
    % cmake -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX" -DLLVM_ENABLE_ASSERTIONS=ON -DCMAKE_BUILD_TYPE=Release ..
    % make -j8

then to point Halide to it:

    export LLVM_CONFIG=<path to llvm>/build/bin/llvm-config
    export CLANG=<path to llvm>/build/bin/clang

On Ubuntu llvm 3.2 also works, but you should omit --disable-terminfo
or -DLLVM_ENABLE_TERMINFO=OFF when configuring it.

With LLVM_CONFIG and CLANG set (or the appropriate llvm-config and
clang in your path), you should be able to just run 'make' in this
directory. 'make run_tests' will run the JIT test suite, and 'make
test_apps' will make sure all the apps compile and run (but won't
check their output).

There is no 'make install' yet. If you want to make an install
package, run 'make distrib'.

If you wish to use cmake to build Halide, the build procedure is:

    % mkdir build
    % cd build
    % cmake ..
    % make -j8

Building Halide and llvm as 32-bit on 64-bit linux
--------------------------------------------------

This is necessary if you want to JIT compile 32-bit code. It is not
necessary for AOT compiling 32-bit Halide pipelines. The 64-bit
version of Halide cross-compiles 32-bit code just fine.

To get a 32-bit llvm, configure and compile it like so:

    % CC="gcc -m32" CXX="g++ -m32" ./configure --enable-targets=x86,arm,nvptx --enable-assertions --enable-optimized --build=i686-pc-linux-gnu
    % CC="gcc -m32" CXX="g++ -m32" make

To generate a 32-bit Halide, compile it like so:

    % HL_TARGET=x86-32 LD="ld -melf_i386" CC="gcc -m32" CXX="g++ -m32" make

You should then be able to run the JIT tests with a 32-bit target:

    % CXX="g++ -m32 -msse4" make build_tests
    % HL_TARGET=x86-32-sse41 make run_tests

If you have a 32-bit libpng, you can also run the apps in 32-bit:

    % HL_TARGET=x86-32-sse41 CXX="g++ -m32 -msse4" make test_apps

The tests should pass, but the tutorials will fail to compile unless
you manually supply a 32-bit libpng.


Building Halide with Native Client support
------------------------------------------

Halide is capable of generating Native Client (NaCl) object files and
Portable Native Client (PNaCl) bitcode.  JIT compilation is not
supported. For both NaCl and PNaCl, the PNaCl llvm tree is used as it
contains required llvm headers and libraries for compiling to all
Native Client targets.

In order to build Halide with Native Client support, one will need the
PNaCl llvm tree from:

    http://git.chromium.org/native_client/pnacl-llvm.git

and, for good measure, PNaCl's version of clang:

    http://git.chromium.org/native_client/pnacl-clang.git

To check these out:

    % git clone http://git.chromium.org/native_client/pnacl-llvm.git pnacl-llvm
    % cd pnacl-llvm/tools
    % git clone http://git.chromium.org/native_client/pnacl-clang.git clang
    % cd ../..

To enable all Halide targets, build it like so:

    % mkdir build
    % cd build
    % cmake -DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX" -DLLVM_ENABLE_ASSERTIONS=ON -DCMAKE_BUILD_TYPE=Release ..
    % make -j8

It will possibly be helpful to get the entire dev tree for
PNaCl. Documentation for this is here:

    http://www.chromium.org/nativeclient/pnacl/developing-pnacl

To use generated code in an application, you'll of course also need
the Native Client SDK:

    https://developer.chrome.com/native-client/sdk/download

Once The Native Client prerequisites are in place, set the following
variables (on the command line or by editing the Makefile):

Point LLVM_CONFIG to the llvm-config that lives in your pnacl llvm build. E.g:

    % export LLVM_CONFIG=<path-to-Halide>/llvm/pnacl-llvm/build/bin/llvm-config

Change WITH_NATIVE_CLIENT to "true" (or any non-empty value):

    % export WITH_NATIVE_CLIENT=true

With these variables set, run make. This will build a Halide lib
capable of generating native client objects. Neither the tests nor
most of the apps Makefiles have been updated to work with cross
compilation however. Try the app HelloNacl for a working example.


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


Writing OpenGL-Based Filters
----------------------------

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


JIT Compilation
---------------

For JIT compilation Halide attempts to load the system libraries for opengl
and creates a new context to use for each module. Windows is not yet supported.

Examples for JIT execution of OpenGL-based filters can be found in test/opengl.


AOT Compilation
---------------

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


Limitations
-----------

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

  * Compute shaders
