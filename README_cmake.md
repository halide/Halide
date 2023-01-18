# Halide and CMake

This is a comprehensive guide to the three main usage stories of the Halide
CMake build.

1. Compiling or packaging Halide from source.
2. Building Halide programs using the official CMake package.
3. Contributing to Halide and updating the build files.

The following sections cover each in detail.

## Table of Contents

- [Getting started](#getting-started)
  - [Installing CMake](#installing-cmake)
  - [Installing dependencies](#installing-dependencies)
- [Building Halide with CMake](#building-halide-with-cmake)
  - [Basic build](#basic-build)
  - [Build options](#build-options)
    - [Find module options](#find-module-options)
- [Using Halide from your CMake build](#using-halide-from-your-cmake-build)
  - [A basic CMake project](#a-basic-cmake-project)
  - [JIT mode](#jit-mode)
  - [AOT mode](#aot-mode)
    - [Autoschedulers](#autoschedulers)
    - [RunGenMain](#rungenmain)
  - [Halide package documentation](#halide-package-documentation)
    - [Components](#components)
    - [Variables](#variables)
    - [Imported targets](#imported-targets)
    - [Functions](#functions)
      - [`add_halide_library`](#add_halide_library)
      - [`add_halide_generator`](#add_halide_generator)
      - [`add_halide_python_extension_library`](#add_halide_python_extension_library)
      - [`add_halide_runtime`](#add_halide_runtime)
- [Contributing CMake code to Halide](#contributing-cmake-code-to-halide)
  - [General guidelines and best practices](#general-guidelines-and-best-practices)
    - [Prohibited commands list](#prohibited-commands-list)
    - [Prohibited variables list](#prohibited-variables-list)
  - [Adding tests](#adding-tests)
  - [Adding apps](#adding-apps)

# Getting started

This section covers installing a recent version of CMake and the correct
dependencies for building and using Halide. If you have not used CMake before,
we strongly suggest reading through the [CMake documentation][cmake-docs] first.

## Installing CMake

Halide requires at least version 3.22, which was released in November 2021.
Fortunately, getting a recent version of CMake couldn't be easier, and there are
multiple good options on any system to do so. Generally, one should always have
the most recent version of CMake installed system-wide. CMake is committed to
backwards compatibility and even the most recent release can build projects over
a decade old.

### Cross-platform

The Python package manager `pip3` has the newest version of CMake at all times.
This might be the most convenient method since Python 3 is an optional
dependency for Halide, anyway.

```
$ pip3 install --upgrade cmake
```

See the [PyPI website][pypi-cmake] for more details.

### Windows

On Windows, there are three primary methods for installing an up-to-date CMake:

1. If you have Visual Studio 2019 installed, you can get CMake 3.17 through the
   Visual Studio installer. This is the recommended way of getting CMake if you
   are able to use Visual Studio 2019. See Microsoft's
   [documentation][vs2019-cmake-docs] for more details.
2. If you use [Chocolatey][chocolatey], its [CMake package][choco-cmake] is kept
   up to date. It should be as simple as `choco install cmake`.
3. Otherwise, you should install CMake from [Kitware's website][cmake-download].

### macOS

On macOS, the [Homebrew][homebrew] [CMake package][brew-cmake] is kept up to
date. Simply run:

```
$ brew update
$ brew install cmake
```

to install the newest version of CMake. If your environment prevents you from
installing Homebrew, the binary release on [Kitware's website][cmake-download]
is also a viable option.

### Ubuntu Linux

There are a few good ways to install a modern CMake on Ubuntu:

1. If you're on Ubuntu Linux 22.04 (Jammy Jellyfish), then simply running
   `sudo apt install cmake` will get you CMake 3.22.
2. If you are on an older Ubuntu release or would like to use the newest CMake,
   try installing via the snap store: `snap install cmake`. Be sure you do not
   already have `cmake` installed via APT. The snap package automatically stays
   up to date.
3. For older versions of Debian, Ubuntu, Mint, and derivatives, Kitware provides
   an [APT repository][cmake-apt] with up-to-date releases. Note that this is
   still useful for Ubuntu 20.04 because it will remain up to date.
4. If all else fails, you might need to build CMake from source (eg. on old
   Ubuntu versions running on ARM). In that case, follow the directions posted
   on [Kitware's website][cmake-from-source].

For other Linux distributions, check with your distribution's package manager or
use pip as detailed above. Snap packages might also be available.

**Note:** On WSL 1, the snap service is not available; in this case, prefer to
use the APT repository. On WSL 2, all methods are available.

## Installing dependencies

We generally recommend using a package manager to fetch Halide's dependencies.
Except where noted, we recommend using [vcpkg][vcpkg] on Windows,
[Homebrew][homebrew] on macOS, and APT on Ubuntu 20.04 LTS.

Only LLVM and Clang are _absolutely_ required to build Halide. Halide always
supports three LLVM versions: the current major version, the previous major
version, and trunk. The LLVM and Clang versions must match exactly. For most
users, we recommend using a binary release of LLVM rather than building it
yourself.

However, to run all of the tests and apps, an extended set is needed. This
includes [lld][lld], [Python 3][python], [libpng][libpng], [libjpeg][libjpeg],
[Doxygen][doxygen], [OpenBLAS][openblas], [ATLAS][atlas], and [Eigen3][eigen].
While not required to build any part of Halide, we find that [Ninja][ninja] is
the best backend build tool across all platforms.

Note that CMake has many special variables for overriding the locations of
packages and executables. A partial list can be found in the
["find module options"](#find-module-options) section below, and more can be
found in the documentation for the CMake [find_package][find_package] command.
Normally, you should prefer to make sure your environment is set up so that
CMake can find dependencies automatically. For instance, if you want CMake to
use a particular version of Python, create a [virtual environment][venv] and
activate it _before_ configuring Halide.

### Windows

We assume you have vcpkg installed at `D:\vcpkg`. Follow the instructions in the
[vcpkg README][vcpkg] to install. Start by installing LLVM.

```
D:\vcpkg> .\vcpkg install llvm[target-all,enable-assertions,clang-tools-extra]:x64-windows
D:\vcpkg> .\vcpkg install llvm[target-all,enable-assertions,clang-tools-extra]:x86-windows
```

This will also install Clang and LLD. The `enable-assertions` option is not
strictly necessary but will make debugging during development much smoother.
These builds will take a long time and a lot of disk space. After they are
built, it is safe to delete the intermediate build files and caches in
`D:\vcpkg\buildtrees` and `%APPDATA%\local\vcpkg`.

Then install the other libraries:

```
D:\vcpkg> .\vcpkg install libpng:x64-windows libjpeg-turbo:x64-windows openblas:x64-windows eigen3:x64-windows
D:\vcpkg> .\vcpkg install libpng:x86-windows libjpeg-turbo:x86-windows openblas:x86-windows eigen3:x86-windows
```

To build the documentation, you will need to install [Doxygen][doxygen]. This
can be done either through [Chocolatey][choco-doxygen] or from the [Doxygen
website][doxygen-download].

```
> choco install doxygen
```

To build the Python bindings, you will need to install Python 3. This should be
done by running the official installer from the [Python website][python]. Be
sure to download the debugging symbols through the installer. This will require
using the "Advanced Installation" workflow. Although it is not strictly
necessary, it is convenient to install Python system-wide on Windows (ie.
`C:\Program Files`). This makes it easy for CMake to find without needing to
manually set the `PATH`.

Once Python is installed, you can install the Python module dependencies either
globally or in a [virtual environment][venv] by running

```
> pip3 install -r .\python_bindings\requirements.txt
```

from the root of the repository.

If you would like to use [Ninja][ninja], note that it is installed alongside
CMake when using the Visual Studio 2019 installer. Alternatively, you can
install via [Chocolatey][choco-ninja] or place the [pre-built
binary][ninja-download] from their website in the PATH.

```
> choco install ninja
```

### macOS

On macOS, it is possible to install all dependencies via [Homebrew][homebrew]:

```
$ brew install llvm libpng libjpeg python@3.8 openblas doxygen ninja
```

The `llvm` package includes `clang`, `clang-format`, and `lld`, too. Don't
forget to install the Python module dependencies:

```
$ pip3 install -r python_bindings/requirements.txt
```

### Ubuntu

Finally, on Ubuntu 20.04 LTS, you should install the following packages (this
includes the Python module dependencies):

```
dev@ubuntu:~$ sudo apt install \
                  clang-tools lld llvm-dev libclang-dev liblld-10-dev \
                  libpng-dev libjpeg-dev libgl-dev \
                  python3-dev python3-numpy python3-scipy python3-imageio python3-pybind11 \
                  libopenblas-dev libeigen3-dev libatlas-base-dev \
                  doxygen ninja-build
```

# Building Halide with CMake

## Basic build

These instructions assume that your working directory is the Halide repo root.

### Windows

If you plan to use the Ninja generator, be sure to be in the developer command
prompt corresponding to your intended environment. Note that whatever your
intended target system (x86, x64, or arm), you must use the 64-bit _host tools_
because the 32-bit tools run out of memory during the linking step with LLVM.
More information is available from [Microsoft's documentation][msvc-cmd].

You should either open the correct Developer Command Prompt directly or run the
[`vcvarsall.bat`][vcvarsall] script with the correct argument, ie. one of the
following:

```
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86
D:\> "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_arm
```

Then, assuming that vcpkg is installed to `D:\vcpkg`, simply run:

```
> cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake -S . -B build
> cmake --build .\build
```

Valid values of [`CMAKE_BUILD_TYPE`][cmake_build_type] are `Debug`,
`RelWithDebInfo`, `MinSizeRel`, and `Release`. When using a single-configuration
generator (like Ninja) you must specify a build type when configuring Halide (or
any other CMake project).

Otherwise, if you wish to create a Visual Studio based build system, you can
configure with:

```
> cmake -G "Visual Studio 16 2019" -Thost=x64 -A x64 ^
        -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
        -S . -B build
> cmake --build .\build --config Release -j %NUMBER_OF_PROCESSORS%
```

Because the Visual Studio generator is a _multi-config generator_, you don't set
`CMAKE_BUILD_TYPE` at configure-time, but instead pass the configuration to the
build (and test/install) commands with the `--config` flag. More documentation
is available in the [CMake User Interaction Guide][cmake-user-interaction].

The process is similar for 32-bit:

```
> cmake -G "Visual Studio 16 2019" -Thost=x64 -A Win32 ^
        -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
        -S . -B build
> cmake --build .\build --config Release -j %NUMBER_OF_PROCESSORS%
```

In both cases, the `-Thost=x64` flag ensures that the correct host tools are
used.

**Note:** due to limitations in MSBuild, incremental builds using the VS
generators will not detect changes to headers in the `src/runtime` folder. We
recommend using Ninja for day-to-day development and use Visual Studio only if
you need it for packaging.

### macOS and Linux

The instructions here are straightforward. Assuming your environment is set up
correctly, just run:

```
dev@host:~/Halide$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
dev@host:~/Halide$ cmake --build ./build
```

If you omit `-G Ninja`, a Makefile-based generator will likely be used instead.
In either case, [`CMAKE_BUILD_TYPE`][cmake_build_type] must be set to one of the
standard types: `Debug`, `RelWithDebInfo`, `MinSizeRel`, or `Release`.

### CMake Presets

If you are using CMake 3.21+, we provide several [presets][cmake_presets] to
make the above commands more convenient. The following CMake preset commands
correspond to the longer ones above.

```
> cmake --preset=win64    # VS 2019 generator, 64-bit build, vcpkg deps
> cmake --preset=win32    # VS 2019 generator, 32-bit build, vcpkg deps
> cmake --preset=release  # Release mode, any single-config generator / compiler

$ cmake --list-presets    # Get full list of presets.
```

The Windows presets assume that the environment variable `VCPKG_ROOT` is set and
points to the root of the vcpkg installation.

## Installing

Once built, Halide will need to be installed somewhere before using it in a
separate project. On any platform, this means running the
[`cmake --install`][cmake-install] command in one of two ways. For a
single-configuration generator (like Ninja), run either:

```
dev@host:~/Halide$ cmake --install ./build --prefix /path/to/Halide-install
> cmake --install .\build --prefix X:\path\to\Halide-install
```

For a multi-configuration generator (like Visual Studio) run:

```
dev@host:~/Halide$ cmake --install ./build --prefix /path/to/Halide-install --config Release
> cmake --install .\build --prefix X:\path\to\Halide-install --config Release
```

Of course, make sure that you build the corresponding config before attempting
to install it.

## Build options

Halide reads and understands several options that can configure the build. The
following are the most consequential and control how Halide is actually
compiled.

| Option                                   | Default               | Description                                                                                                      |
|------------------------------------------|-----------------------|------------------------------------------------------------------------------------------------------------------|
| [`BUILD_SHARED_LIBS`][build_shared_libs] | `ON`                  | Standard CMake variable that chooses whether to build as a static or shared library.                             |
| `Halide_BUNDLE_LLVM`                     | `OFF`                 | When building Halide as a static library, unpack the LLVM static libraries and add those objects to libHalide.a. |
| `Halide_SHARED_LLVM`                     | `OFF`                 | Link to the shared version of LLVM. Not available on Windows.                                                    |
| `Halide_ENABLE_RTTI`                     | _inherited from LLVM_ | Enable RTTI when building Halide. Recommended to be set to `ON`                                                  |
| `Halide_ENABLE_EXCEPTIONS`               | `ON`                  | Enable exceptions when building Halide                                                                           |
| `Halide_TARGET`                          | _empty_               | The default target triple to use for `add_halide_library` (and the generator tests, by extension)                |

The following options are _advanced_ and should not be required in typical workflows. Generally, these are used by
Halide's own CI infrastructure, or as escape hatches for third-party packagers.

| Option                      | Default                                                            | Description                                                                              |
|-----------------------------|--------------------------------------------------------------------|------------------------------------------------------------------------------------------|
| `Halide_CLANG_TIDY_BUILD`   | `OFF`                                                              | Used internally to generate fake compile jobs for runtime files when running clang-tidy. |
| `Halide_CCACHE_BUILD`       | `OFF`                                                              | Use ccache with Halide-recommended settings to accelerate rebuilds.                      |
| `Halide_CCACHE_PARAMS`      | `CCACHE_CPP2=yes CCACHE_HASHDIR=yes CCACHE_SLOPPINESS=pch_defines` | Options to pass to `ccache` when using `Halide_CCACHE_BUILD`.                            |
| `Halide_SOVERSION_OVERRIDE` | `${Halide_VERSION_MAJOR}`                                          | Override the SOVERSION for libHalide. Expects a positive integer (i.e. not a version).   |

The following options are only available when building Halide directly, ie. not
through the [`add_subdirectory`][add_subdirectory] or
[`FetchContent`][fetchcontent] mechanisms. They control whether non-essential
targets (like tests and documentation) are built.

| Option                 | Default              | Description                                                      |
|------------------------|----------------------|------------------------------------------------------------------|
| `WITH_TESTS`           | `ON`                 | Enable building unit and integration tests                       |
| `WITH_PYTHON_BINDINGS` | `ON` if Python found | Enable building Python 3.x bindings                              |
| `WITH_DOCS`            | `OFF`                | Enable building the documentation via Doxygen                    |
| `WITH_UTILS`           | `ON`                 | Enable building various utilities including the trace visualizer |
| `WITH_TUTORIALS`       | `ON`                 | Enable building the tutorials                                    |

The following options control whether to build certain test subsets. They only
apply when `WITH_TESTS=ON`:

| Option                    | Default | Description                       |
|---------------------------|---------|-----------------------------------|
| `WITH_TEST_AUTO_SCHEDULE` | `ON`    | enable the auto-scheduling tests  |
| `WITH_TEST_CORRECTNESS`   | `ON`    | enable the correctness tests      |
| `WITH_TEST_ERROR`         | `ON`    | enable the expected-error tests   |
| `WITH_TEST_WARNING`       | `ON`    | enable the expected-warning tests |
| `WITH_TEST_PERFORMANCE`   | `ON`    | enable performance testing        |
| `WITH_TEST_GENERATOR`     | `ON`    | enable the AOT generator tests    |

The following options enable/disable various LLVM backends (they correspond to
LLVM component names):

| Option               | Default              | Description                         |
|----------------------|----------------------|-------------------------------------|
| `TARGET_AARCH64`     | `ON`, _if available_ | Enable the AArch64 backend          |
| `TARGET_AMDGPU`      | `ON`, _if available_ | Enable the AMD GPU backend          |
| `TARGET_ARM`         | `ON`, _if available_ | Enable the ARM backend              |
| `TARGET_HEXAGON`     | `ON`, _if available_ | Enable the Hexagon backend          |
| `TARGET_NVPTX`       | `ON`, _if available_ | Enable the NVidia PTX backend       |
| `TARGET_POWERPC`     | `ON`, _if available_ | Enable the PowerPC backend          |
| `TARGET_RISCV`       | `ON`, _if available_ | Enable the RISC V backend           |
| `TARGET_WEBASSEMBLY` | `ON`, _if available_ | Enable the WebAssembly backend.     |
| `TARGET_X86`         | `ON`, _if available_ | Enable the x86 (and x86_64) backend |

The following options enable/disable various Halide-specific backends:

| Option                | Default | Description                            |
|-----------------------|---------|----------------------------------------|
| `TARGET_OPENCL`       | `ON`    | Enable the OpenCL-C backend            |
| `TARGET_METAL`        | `ON`    | Enable the Metal backend               |
| `TARGET_D3D12COMPUTE` | `ON`    | Enable the Direct3D 12 Compute backend |

The following options are WebAssembly-specific. They only apply when
`TARGET_WEBASSEMBLY=ON`:

| Option      | Default | Description                               |
|-------------|---------|-------------------------------------------|
| `WITH_WABT` | `ON`    | Include WABT Interpreter for WASM testing |

### Find module options

Halide uses the following find modules to search for certain dependencies. These
modules accept certain variables containing hints for the search process. Before
setting any of these variables, closely study the [`find_package`][find_package]
documentation.

All of these variables should be set at the CMake command line via the `-D`
flag.

First, Halide expects to find LLVM and Clang through the `CONFIG` mode of
`find_package`. You can tell Halide where to find these dependencies by setting
the corresponding `_DIR` variables:

| Variable    | Description                                    |
|-------------|------------------------------------------------|
| `LLVM_DIR`  | `$LLVM_ROOT/lib/cmake/LLVM/LLVMConfig.cmake`   |
| `Clang_DIR` | `$LLVM_ROOT/lib/cmake/Clang/ClangConfig.cmake` |

Here, `$LLVM_ROOT` is assumed to point to the root of an LLVM installation tree.
This is either a system path or one produced by running `cmake --install` (as
detailed in the main README.md). When building LLVM (and any other `CONFIG`
packages) manually, it is a common mistake to point CMake to a _build tree_
rather than an _install tree_. Doing so often produces inscrutable errors.

When using CMake 3.18 or above, some of Halide's tests will search for CUDA
using the [`FindCUDAToolkit`][findcudatoolkit] module. If it doesn't find your
CUDA installation automatically, you can point it to it by setting:

| Variable           | Description                                       |
|--------------------|---------------------------------------------------|
| `CUDAToolkit_ROOT` | Path to the directory containing `bin/nvcc[.exe]` |
| `CUDA_PATH`        | _Environment_ variable, same as above.            |

If the CMake version is lower than 3.18, the deprecated [`FindCUDA`][findcuda]
module will be used instead. It reads the variable `CUDA_TOOLKIT_ROOT_DIR`
instead of `CUDAToolkit_ROOT` above.

TODO(https://github.com/halide/Halide/issues/5633): update this section for OpenGLCompute, which needs some (but maybe not all) of this.

When targeting OpenGL, the [`FindOpenGL`][findopengl] and [`FindX11`][findx11]
modules will be used to link AOT generated binaries. These modules can be
overridden by setting the following variables:

| Variable                | Description                      |
|-------------------------|----------------------------------|
| `OPENGL_egl_LIBRARY`    | Path to the EGL library.         |
| `OPENGL_glu_LIBRARY`    | Path to the GLU library.         |
| `OPENGL_glx_LIBRARY`    | Path to the GLVND GLX library.   |
| `OPENGL_opengl_LIBRARY` | Path to the GLVND OpenGL library |
| `OPENGL_gl_LIBRARY`     | Path to the OpenGL library.      |

The OpenGL paths will need to be set if you intend to use OpenGL with X11 on
macOS.

Halide also searches for `libpng` and `libjpeg-turbo` through the
[`FindPNG`][findpng] and [`FindJPEG`][findjpeg] modules, respectively. They can
be overridden by setting the following variables.

| Variable            | Description                                        |
|---------------------|----------------------------------------------------|
| `PNG_LIBRARIES`     | Paths to the libraries to link against to use PNG. |
| `PNG_INCLUDE_DIRS`  | Path to `png.h`, etc.                              |
| `JPEG_LIBRARIES`    | Paths to the libraries needed to use JPEG.         |
| `JPEG_INCLUDE_DIRS` | Paths to `jpeglib.h`, etc.                         |

When `WITH_DOCS` is set to `ON`, Halide searches for Doxygen using the
[`FindDoxygen`][finddoxygen] module. It can be overridden by setting the
following variable.

| Variable             | Description                     |
|----------------------|---------------------------------|
| `DOXYGEN_EXECUTABLE` | Path to the Doxygen executable. |

When compiling for an OpenCL target, Halide uses the [`FindOpenCL`][findopencl]
target to locate the libraries and include paths. These can be overridden by
setting the following variables:

| Variable              | Description                                           |
|-----------------------|-------------------------------------------------------|
| `OpenCL_LIBRARIES`    | Paths to the libraries to link against to use OpenCL. |
| `OpenCL_INCLUDE_DIRS` | Include directories for OpenCL.                       |

Lastly, Halide searches for Python 3 using the [`FindPython3`][findpython3]
module, _not_ the deprecated `FindPythonInterp` and `FindPythonLibs` modules,
like other projects you might have encountered. You can select which Python
installation to use by setting the following variable.

| Variable           | Description                                           |
|--------------------|-------------------------------------------------------|
| `Python3_ROOT_DIR` | Define the root directory of a Python 3 installation. |

# Using Halide from your CMake build

This section assumes some basic familiarity with CMake but tries to be explicit
in all its examples. To learn more about CMake, consult the
[documentation][cmake-docs] and engage with the community on the [CMake
Discourse][cmake-discourse].

Note: previous releases bundled a `halide.cmake` module that was meant to be
[`include()`][include]-ed into your project. This has been removed. Please
upgrade to the new package config module.

## A basic CMake project

There are two main ways to use Halide in your application: as a **JIT compiler**
for dynamic pipelines or an **ahead-of-time (AOT) compiler** for static
pipelines. CMake provides robust support for both use cases.

No matter how you intend to use Halide, you will need some basic CMake
boilerplate.

```cmake
cmake_minimum_required(VERSION 3.22)
project(HalideExample)

set(CMAKE_CXX_STANDARD 17)  # or newer
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)
```

The [`cmake_minimum_required`][cmake_minimum_required] command is required to be
the first command executed in a CMake program. It disables all of the deprecated
behavior ("policies" in CMake lingo) from earlier versions. The
[`project`][project] command sets the name of the project (and has arguments for
versioning, language support, etc.) and is required by CMake to be called
immediately after setting the minimum version.

The next three variables set the project-wide C++ standard. The first,
[`CMAKE_CXX_STANDARD`][cmake_cxx_standard], simply sets the standard version.
Halide requires at least C++17. The second,
[`CMAKE_CXX_STANDARD_REQUIRED`][cmake_cxx_standard_required], tells CMake to
fail if the compiler cannot provide the requested standard version. Lastly,
[`CMAKE_CXX_EXTENSIONS`][cmake_cxx_extensions] tells CMake to disable
vendor-specific extensions to C++. This is not necessary to simply use Halide,
but we require it when authoring new code in the Halide repo.

Finally, we use [`find_package`][find_package] to locate Halide on your system.
If Halide is not globally installed, you will need to add the root of the Halide
installation directory to [`CMAKE_PREFIX_PATH`][cmake_prefix_path] at the CMake
command line.

```
dev@ubuntu:~/myproj$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/path/to/Halide-install" -S . -B build
```

## JIT mode

To use Halide in JIT mode (like the [tutorials][halide-tutorials] do, for
example), you can simply link to `Halide::Halide`.

```cmake
# ... same project setup as before ...
add_executable(my_halide_app main.cpp)
target_link_libraries(my_halide_app PRIVATE Halide::Halide)
```

Then `Halide.h` will be available to your code and everything should just work.
That's it!

## AOT mode

Using Halide in AOT mode is more complicated so we'll walk through it step by
step. Note that this only applies to Halide generators, so it might be useful to
re-read the [tutorial][halide-generator-tutorial] on generators. Assume (like in
the tutorial) that you have a source file named `my_generators.cpp` and that in
it you have generator classes `MyFirstGenerator` and `MySecondGenerator` with
registered names `my_first_generator` and `my_second_generator` respectively.

Then the first step is to add a **generator executable** to your build:

```cmake
# ... same project setup as before ...
add_executable(my_generators my_generators.cpp)
target_link_libraries(my_generators PRIVATE Halide::Generator)
```

Using the generator executable, we can add a Halide library corresponding to
`MyFirstGenerator`.

```cmake
# ... continuing from above
add_halide_library(my_first_generator FROM my_generators)
```

This will create a static library target in CMake that corresponds to the output
of running your generator. The second generator in the file requires generator
parameters to be passed to it. These are also easy to handle:

```cmake
# ... continuing from above
add_halide_library(my_second_generator FROM my_generators
                   PARAMS parallel=false scale=3.0 rotation=ccw output.type=uint16)
```

Adding multiple configurations is easy, too:

```cmake
# ... continuing from above
add_halide_library(my_second_generator_2 FROM my_generators
                   GENERATOR my_second_generator
                   PARAMS scale=9.0 rotation=ccw output.type=float32)

add_halide_library(my_second_generator_3 FROM my_generators
                   GENERATOR my_second_generator
                   PARAMS parallel=false output.type=float64)
```

Here, we had to specify which generator to use (`my_second_generator`) since it
uses the target name by default. The functions in these libraries will be named
after the target names, `my_second_generator_2` and `my_second_generator_3`, by
default, but it is possible to control this via the `FUNCTION_NAME` parameter.

Each one of these targets, `<GEN>`, carries an associated `<GEN>.runtime`
target, which is also a static library containing the Halide runtime. It is
transitively linked through `<GEN>` to targets that link to `<GEN>`. On an
operating system like Linux, where weak linking is available, this is not an
issue. However, on Windows, this can fail due to symbol redefinitions. In these
cases, you must declare that two Halide libraries share a runtime, like so:

```cmake
# ... updating above
add_halide_library(my_second_generator_2 FROM my_generators
                   GENERATOR my_second_generator
                   USE_RUNTIME my_first_generator.runtime
                   PARAMS scale=9.0 rotation=ccw output.type=float32)

add_halide_library(my_second_generator_3 FROM my_generators
                   GENERATOR my_second_generator
                   USE_RUNTIME my_first_generator.runtime
                   PARAMS parallel=false output.type=float64)
```

This will even work correctly when different combinations of targets are
specified for each halide library. A "greatest common denominator" target will
be chosen that is compatible with all of them (or the build will fail).

### Autoschedulers

When the autoschedulers are included in the release package, they are very
simple to apply to your own generators. For example, we could update the
definition of the `my_first_generator` library above to use the `Adams2019`
autoscheduler:

```cmake
add_halide_library(my_second_generator FROM my_generators
                   AUTOSCHEDULER Halide::Adams2019)
```

### RunGenMain

Halide provides a generic driver for generators to be used during development
for benchmarking and debugging. Suppose you have a generator executable called
`my_gen` and a generator within called `my_filter`. Then you can pass a variable
name to the `REGISTRATION` parameter of `add_halide_library` which will contain
the name of a generated C++ source that should be linked to `Halide::RunGenMain`
and `my_filter`.

For example:

```cmake
add_halide_library(my_filter FROM my_gen
                   REGISTRATION filter_reg_cpp)
add_executable(runner ${filter_reg_cpp})
target_link_libraries(runner PRIVATE my_filter Halide::RunGenMain)
```

Then you can run, debug, and benchmark your generator through the `runner`
executable.

## Halide package documentation

Halide provides a CMake _package configuration_ module. The intended way to use
the CMake build is to run `find_package(Halide ...)` in your `CMakeLists.txt`
file. Closely read the [`find_package` documentation][find_package] before
proceeding.

### Components

The Halide package script understands a handful of optional components when
loading the package.

First, if you plan to use the Halide Image IO library, you will want to include
the `png` and `jpeg` components when loading Halide.

Second, Halide releases can contain a variety of configurations: static, shared,
debug, release, etc. CMake handles Debug/Release configurations automatically,
but generally only allows one type of library to be loaded.

The package understands two components, `static` and `shared`, that specify
which type of library you would like to load. For example, if you want to make
sure that you link against shared Halide, you can write:

```cmake
find_package(Halide REQUIRED COMPONENTS shared)
```

If the shared libraries are not available, this will result in a failure.

If no component is specified, then the `Halide_SHARED_LIBS` variable is checked.
If it is defined and set to true, then the shared libraries will be loaded or
the package loading will fail. Similarly, if it is defined and set to false, the
static libraries will be loaded.

If no component is specified and `Halide_SHARED_LIBS` is _not_ defined, then the
[`BUILD_SHARED_LIBS`][build_shared_libs] variable will be inspected. If it is
**not defined** or **defined and set to true**, then it will attempt to load the
shared libs and fall back to the static libs if they are not available.
Similarly, if `BUILD_SHARED_LIBS` is **defined and set to false**, then it will
try the static libs first then fall back to the shared libs.

### Variables

Variables that control package loading:

| Variable             | Description                                                                                                                                                                   |
|----------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Halide_SHARED_LIBS` | override `BUILD_SHARED_LIBS` when loading the Halide package via `find_package`. Has no effect when using Halide via `add_subdirectory` as a Git or `FetchContent` submodule. |

Variables set by the package:

| Variable                   | Description                                                        |
|----------------------------|--------------------------------------------------------------------|
| `Halide_VERSION`           | The full version string of the loaded Halide package               |
| `Halide_VERSION_MAJOR`     | The major version of the loaded Halide package                     |
| `Halide_VERSION_MINOR`     | The minor version of the loaded Halide package                     |
| `Halide_VERSION_PATCH`     | The patch version of the loaded Halide package                     |
| `Halide_VERSION_TWEAK`     | The tweak version of the loaded Halide package                     |
| `Halide_HOST_TARGET`       | The Halide target triple corresponding to "host" for this build.   |
| `Halide_CMAKE_TARGET`      | The Halide target triple corresponding to the active CMake target. |
| `Halide_ENABLE_EXCEPTIONS` | Whether Halide was compiled with exception support                 |
| `Halide_ENABLE_RTTI`       | Whether Halide was compiled with RTTI                              |

### Imported targets

Halide defines the following targets that are available to users:

| Imported target      | Description                                                                                                                          |
|----------------------|--------------------------------------------------------------------------------------------------------------------------------------|
| `Halide::Halide`     | this is the JIT-mode library to use when using Halide from C++.                                                                      |
| `Halide::Generator`  | this is the target to use when defining a generator executable. It supplies a `main()` function.                                     |
| `Halide::Runtime`    | adds include paths to the Halide runtime headers                                                                                     |
| `Halide::Tools`      | adds include paths to the Halide tools, including the benchmarking utility.                                                          |
| `Halide::ImageIO`    | adds include paths to the Halide image IO utility and sets up dependencies to PNG / JPEG if they are available.                      |
| `Halide::RunGenMain` | used with the `REGISTRATION` parameter of `add_halide_library` to create simple runners and benchmarking tools for Halide libraries. |

The following targets are not guaranteed to be available:

| Imported target         | Description                                                                                                                                                       |
|-------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Halide::Python`        | this is a Python 3 package that can be referenced as `$<TARGET_FILE_DIR:Halide::Python>/..` when setting up `PYTHONPATH` for Python tests or the like from CMake. |
| `Halide::Adams19`       | the Adams et.al. 2019 autoscheduler (no GPU support)                                                                                                              |
| `Halide::Li18`          | the Li et.al. 2018 gradient autoscheduler (limited GPU support)                                                                                                   |
| `Halide::Mullapudi2016` | the Mullapudi et.al. 2016 autoscheduler (no GPU support)                                                                                                          |

### Functions

Currently, only two functions are defined:

#### `add_halide_library`

This is the main function for managing generators in AOT compilation. The full
signature follows:

```
add_halide_library(<target> FROM <generator-target>
                   [GENERATOR generator-name]
                   [FUNCTION_NAME function-name]
                   [NAMESPACE cpp-namespace]
                   [USE_RUNTIME hl-target]
                   [PARAMS param1 [param2 ...]]
                   [TARGETS target1 [target2 ...]]
                   [FEATURES feature1 [feature2 ...]]
                   [PLUGINS plugin1 [plugin2 ...]]
                   [AUTOSCHEDULER scheduler-name]
                   [GRADIENT_DESCENT]
                   [C_BACKEND]
                   [REGISTRATION OUTVAR]
                   [HEADER OUTVAR]
                   [FUNCTION_INFO_HEADER OUTVAR]
                   [<extra-output> OUTVAR])

extra-output = ASSEMBLY | BITCODE | COMPILER_LOG | FEATURIZATION
             | LLVM_ASSEMBLY | PYTHON_EXTENSION
             | PYTORCH_WRAPPER | SCHEDULE | STMT | STMT_HTML
```

This function creates a called `<target>` corresponding to running the
`<generator-target>` (an executable target which links to `Halide::Generator`)
one time, using command line arguments derived from the other parameters.

The arguments `GENERATOR` and `FUNCTION_NAME` default to `<target>`. They
correspond to the `-g` and `-f` command line flags, respectively.

`NAMESPACE` is syntactic sugar to specify the C++ namespace (if any) of the
generated function; you can also specify the C++ namespace (if any) directly
in the `FUNCTION_NAME` argument, but for repeated declarations or very long
namespaces, specifying this separately can provide more readable build files.

If `USE_RUNTIME` is not specified, this function will create another target
called `<target>.runtime` which corresponds to running the generator with `-r`
and a compatible list of targets. This runtime target is an INTERFACE dependency
of `<target>`. If multiple runtime targets need to be linked together, setting
`USE_RUNTIME` to another Halide library, `<target2>` will prevent the generation
of `<target>.runtime` and instead use `<target2>.runtime`. This argument is
most commonly used in conjunction with [`add_halide_runtime`](#add_halide_runtime).

Parameters can be passed to a generator via the `PARAMS` argument. Parameters
should be space-separated. Similarly, `TARGETS` is a space-separated list of
targets for which to generate code in a single function. They must all share the
same platform/bits/os triple (eg. `arm-32-linux`). Features that are in common
among all targets, including device libraries (like `cuda`) should go in
`FEATURES`. If `TARGETS` is not specified, the value of `Halide_TARGET` specified
at configure time will be used.

Every element of `TARGETS` must begin with the same `arch-bits-os` triple. This
function understands two _meta-triples_, `host` and `cmake`. The meta-triple
`host` is equal to the `arch-bits-os` triple used to compile Halide along with
all of the supported instruction set extensions. On platforms that support
running both 32 and 64-bit programs, this will not necessarily equal the
platform the compiler is running on or that CMake is targeting.

The meta-triple `cmake` is equal to the `arch-bits-os` of the current CMake
target. This is useful if you want to make sure you are not unintentionally
cross-compiling, which would result in an [`IMPORTED` target][imported-target]
being created. When `TARGETS` is empty and the `host` target would not
cross-compile, then `host` will be used. Otherwise, `cmake` will be used and an
author warning will be issued.

To use an autoscheduler, set the `AUTOSCHEDULER` argument to a target
named like `Namespace::Scheduler`, for example `Halide::Adams19`. This will set
the `autoscheduler` GeneratorParam on the generator command line to `Scheduler` and add the target to
the list of plugins. Additional plugins can be loaded by setting the `PLUGINS`
argument. If the argument to `AUTOSCHEDULER` does not contain `::` or it does
not name a target, it will be passed to the `-s` flag verbatim.

If `GRADIENT_DESCENT` is set, then the module will be built suitably for
gradient descent calculation in TensorFlow or PyTorch. See
`Generator::build_gradient_module()` for more documentation. This corresponds to
passing `-d 1` at the generator command line.

If the `C_BACKEND` option is set, this command will invoke the configured C++
compiler on a generated source. Note that a `<target>.runtime` target is _not_
created in this case, and the `USE_RUNTIME` option is ignored. Other options
work as expected.

If `REGISTRATION` is set, the path (relative to `CMAKE_CURRENT_BINARY_DIR`)
to the generated `.registration.cpp` file will be set in `OUTVAR`. This can be
used to generate a runner for a Halide library that is useful for benchmarking
and testing, as documented above. This is equivalent to setting
`-e registration` at the generator command line.

If `HEADER` is set, the path (relative to `CMAKE_CURRENT_BINARY_DIR`) to the
generated `.h` header file will be set in `OUTVAR`. This can be used with
`install(FILES)` to conveniently deploy the generated header along with your
library.

If `FUNCTION_INFO_HEADER` is set, the path (relative to
`CMAKE_CURRENT_BINARY_DIR`) to the generated `.function_info.h` header file
will be set in `OUTVAR`. This produces a file that contains `constexpr`
descriptions of information about the generated functions (e.g., argument
type and information). It is generated separately from the normal `HEADER`
file because `HEADER` is intended to work with basic `extern "C"` linkage,
while `FUNCTION_INFO_HEADER` requires C++17 or later to use effectively.
(This can be quite useful for advanced usages, such as producing automatic
call wrappers, etc.) Examples of usage can be found in the generated file.

Lastly, each of the `extra-output` arguments directly correspond to an extra
output (via `-e`) from the generator. The value `OUTVAR` names a variable into
which a path (relative to
[`CMAKE_CURRENT_BINARY_DIR`][cmake_current_binary_dir]) to the extra file will
be written.

#### `add_halide_generator`

This function aids in creating cross-compilable builds that use Halide generators.

```
add_halide_generator(
    target
    [PACKAGE_NAME package-name]
    [PACKAGE_NAMESPACE namespace]
    [EXPORT_FILE export-file]
    [PYSTUB generator-name]
    [[SOURCES] source1 ...]
)
```

Every named argument is optional, and the function uses the following default arguments:

- If `PACKAGE_NAME` is not provided, it defaults to `${PROJECT_NAME}-halide_generators`.
- If `PACKAGE_NAMESPACE` is not provided, it defaults to `${PROJECT_NAME}::halide_generators::`.
- If `EXPORT_FILE` is not provided, it defaults to `${PROJECT_BINARY_DIR}/cmake/${ARG_PACKAGE_NAME}-config.cmake`

The `SOURCES` keyword marks the beginning of sources to be used to build
`<target>`, if it is not loaded. All unparsed arguments will be interpreted as
sources.

This function guarantees that a Halide generator target named
`<namespace><target>` is available. It will first search for a package named
`<package-name>` using `find_package`; if it is found, it is assumed that it
provides the target. Otherwise, it will create an executable target named
`target` and an `ALIAS` target `<namespace><target>`. This function also
creates a custom target named `<package-name>` if it does not exist and
`<target>` would exist. In this case, `<package-name>` will depend on
`<target>`, this enables easy building of _just_ the Halide generators managed
by this function.

After the call, `<PACKAGE_NAME>_FOUND` will be set to true if the host
generators were imported (and hence won't be built). Otherwise, it will be set
to false. This variable may be used to conditionally set properties on
`<target>`.

Please see [test/integration/xc](https://github.com/halide/Halide/tree/main/test/integration/xc) for a simple example
and [apps/hannk](https://github.com/halide/Halide/tree/main/apps/hannk) for a complete app that uses it extensively.

If `PYSTUB` is specified, then a Python Extension will be built that
wraps the Generator with CPython glue to allow use of the Generator
Python 3.x. The result will be a a shared library of the form
`<target>_pystub.<soabi>.so`, where <soabi> describes the specific Python version and platform (e.g., `cpython-310-darwin` for Python 3.10 on macOS.) See
`README_python.md` for examples of use.

#### `add_halide_python_extension_library`

This function wraps the outputs of one or more `add_halide_library` targets with glue code to produce
a Python Extension library.

```
add_halide_python_extension_library(
    target
    [MODULE_NAME module-name]
    HALIDE_LIBRARIES library1 ...
)
```

`FROM` specifies any valid Generator target. If omitted,

`HALIDE_LIBRARIES` is a list of one of more `add_halide_library` targets. Each will be added to the
extension as a callable method of the module. Note that every library specified must be built with
the `PYTHON_EXTENSION` keyword specified, and all libraries must use the same Halide runtime.

The result will be a a shared library of the form
`<target>.<soabi>.so`, where <soabi> describes the specific Python version and
platform (e.g., `cpython-310-darwin` for Python 3.10 on macOS.)

#### `add_halide_runtime`

This function generates a library containing a Halide runtime. Most user code will never
need to use this, as `add_halide_library()` will call it for you if necessary. The most common
use case is usually in conjunction with `add_halide_python_extension_library()`, as a way to
ensure that all the halide libraries share an identical runtime.

```
add_halide_runtime(
    target
    [TARGETS target1 [target2 ...]]
)
```

The `TARGETS` argument has identical semantics to the argument of the same name
for [`add_halide_library`](#add_halide_library).

## Cross compiling

Cross-compiling in CMake can be tricky, since CMake doesn't easily support
compiling for both the host platform and the cross-platform within the same
build. Unfortunately, Halide generator executables are just about always
designed to run on the host platform. Each project will be set up differently
and have different requirements, but here are some suggestions for effective use
of CMake in these scenarios.

### Use `add_halide_generator`

If you are writing new programs that use Halide, you might wish to use our
helper, `add_halide_generator`. When using this helper, you are expected to
build your project twice: once for your build host and again for your intended
target.

When building the host build, you can use the `<package-name>` (see the
documentation above) target to build _just_ the generators. Then, in the
target build, set `<package-name>_ROOT` to the host build directory.

For example:

```
$ cmake -G Ninja -S . -B build-host -DCMAKE_BUILD_TYPE=Release
$ cmake --build build-host --target <package-name>
$ cmake -G Ninja -S . -B build-target -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/target-tc.cmake \
    -D<package-name>_ROOT:FILEPATH=$PWD/build-host
$ cmake --build build-target
```

### Use a super-build

A CMake super-build consists of breaking down a project into sub-projects that
are isolated by [toolchain][cmake-toolchains]. The basic structure is to have an
outermost project that only coordinates the sub-builds via the
[`ExternalProject`][externalproject] module.

One would then use Halide to build a generator executable in one self-contained
project, then export that target to be used in a separate project. The second
project would be configured with the target [toolchain][cmake-toolchains] and
would call `add_halide_library` with no `TARGETS` option and set `FROM` equal to
the name of the imported generator executable. Obviously, this is a significant
increase in complexity over a typical CMake project.

This is very compatible with the `add_halide_generator` strategy above.

### Use `ExternalProject` directly

A lighter weight alternative to the above is to use
[`ExternalProject`][externalproject] directly in your parent build. Configure
the parent build with the target [toolchain][cmake-toolchains], and configure
the inner project to use the host toolchain. Then, manually create an
[`IMPORTED` target][imported-executable] for your generator executable and call
`add_halide_library` as described above.

The main drawback of this approach is that creating accurate `IMPORTED` targets
is difficult since predicting the names and locations of your binaries across
all possible platform and CMake project generators is difficult. In particular,
it is hard to predict executable extensions in cross-OS builds.

### Use an emulator or run on device

The [`CMAKE_CROSSCOMPILING_EMULATOR`][cmake_crosscompiling_emulator] variable
allows one to specify a command _prefix_ to run a target-system binary on the
host machine. One could set this to a custom shell script that uploads the
generator executable, runs it on the device and copies back the results.

### Bypass CMake

The previous two options ensure that the targets generated by
`add_halide_library` will be _normal_ static libraries. This approach does not
use [`ExternalProject`][externalproject], but instead produces `IMPORTED`
targets. The main drawback of `IMPORTED` targets is that they are considered
second-class in CMake. In particular, they cannot be installed with the typical
[`install(TARGETS)` command][install-targets]. Instead, they must be installed
using [`install(FILES)`][install-files] and the
[`$<TARGET_FILE:tgt>`][target-file] generator expression.

# Contributing CMake code to Halide

When contributing new CMake code to Halide, keep in mind that the minimum
version is 3.22. Therefore, it is possible (and indeed required) to use modern
CMake best practices.

Like any large and complex system with a dedication to preserving backwards
compatibility, CMake is difficult to learn and full of traps. While not
comprehensive, the following serves as a guide for writing quality CMake code
and outlines the code quality expectations we have as they apply to CMake.

## General guidelines and best practices

The following are some common mistakes that lead to subtly broken builds.

- **Reading the build directory.** While setting up the build, the build
  directory should be considered _write only_. Using the build directory as a
  read/write temporary directory is acceptable as long as all temp files are
  cleaned up by the end of configuration.
- **Not using [generator expressions][cmake-genex].** Declarative is better than
  imperative and this is no exception. Conditionally adding to a target property
  can leak unwanted details about the build environment into packages. Some
  information is not accurate or available except via generator expressions, eg.
  the build configuration.
- **Using the wrong variable.** `CMAKE_SOURCE_DIR` doesn't always point to the
  Halide source root. When someone uses Halide via
  [`FetchContent`][fetchcontent], it will point to _their_ source root instead.
  The correct variable is [`Halide_SOURCE_DIR`][project-name_source_dir]. If you
  want to know if the compiler is MSVC, check it directly with the
  [`MSVC`][msvc] variable; don't use [`WIN32`][win32]. That will be wrong when
  compiling with clang on Windows. In most cases, however, a generator
  expression will be more appropriate.
- **Using directory properties.** Directory properties have vexing behavior and
  are essentially deprecated from CMake 3.0+. Propagating target properties is
  the way of the future.
- **Using the wrong visibility.** Target properties can be `PRIVATE`,
  `INTERFACE`, or both (aka `PUBLIC`). Pick the most conservative one for each
  scenario. Refer to the [transitive usage requirements][cmake-propagation] docs
  for more information.
- **Needlessly expanding variables** The [`if`][cmake_if] and
  [`foreach`][cmake_foreach] commands generally expand variables when provided by
  name. Expanding such variables manually can unintentionally change the behavior
  of the command. Use `foreach (item IN LISTS list)` instead of
  `foreach (item ${list})`. Similarly, use `if (varA STREQUAL varB)` instead of
  `if ("${varA}" STREQUAL "${varB}")` and _definitely_ don't use
  `if (${varA} STREQUAL ${varB})` since that will fail (in the best case) if
  either variable's value contains a semi-colon (due to argument expansion).

### Prohibited commands list

As mentioned above, using directory properties is brittle and they are therefore
_not allowed_. The following functions may not appear in any new CMake code.

| Command                             | Alternative                                                                                        |
|-------------------------------------|----------------------------------------------------------------------------------------------------|
| `add_compile_definitions`           | Use [`target_compile_definitions`][target_compile_definitions]                                     |
| `add_compile_options`               | Use [`target_compile_options`][target_compile_options]                                             |
| `add_definitions`                   | Use [`target_compile_definitions`][target_compile_definitions]                                     |
| `add_link_options`                  | Use [`target_link_options`][target_link_options], but prefer not to use either                     |
| `get_directory_property`            | Use cache variables or target properties                                                           |
| `get_property(... DIRECTORY)`       | Use cache variables or target properties                                                           |
| `include_directories`               | Use [`target_include_directories`][target_include_directories]                                     |
| `link_directories`                  | Use [`target_link_libraries`][target_link_libraries]                                               |
| `link_libraries`                    | Use [`target_link_libraries`][target_link_libraries]                                               |
| `remove_definitions`                | [Generator expressions][cmake-genex] in [`target_compile_definitions`][target_compile_definitions] |
| `set_directory_properties`          | Use cache variables or target properties                                                           |
| `set_property(... DIRECTORY)`       | Use cache variables or target properties                                                           |
| `target_link_libraries(target lib)` | Use [`target_link_libraries`][target_link_libraries] _with a visibility specifier_ (eg. `PRIVATE`) |

As an example, it was once common practice to write code similar to this:

```cmake
# WRONG: do not do this
include_directories(include)
add_library(my_lib source1.cpp ..)
```

However, this has two major pitfalls. First, it applies to _all_ targets created
in that directory, even those before the call to `include_directories` and those
created in [`include()`][include]-ed CMake files. As CMake files get larger and
more complex, this behavior gets harder to pinpoint. This is particularly vexing
when using the `link_libraries` or `add_defintions` commands. Second, this form
does not provide a way to _propagate_ the include directory to consumers of
`my_lib`. The correct way to do this is:

```cmake
# CORRECT
add_library(my_lib source1.cpp ...)
target_include_directories(my_lib PUBLIC $<BUILD_INTERFACE:include>)
```

This is better in many ways. It only affects the target in question. It
propagates the include path to the targets linking to it (via `PUBLIC`). It also
does not incorrectly export the host-filesystem-specific include path when
installing or packaging the target (via `$<BUILD_INTERFACE>`).

If common properties need to be grouped together, use an INTERFACE target
(better) or write a function (worse). There are also several functions that are
disallowed for other reasons:

| Command                         | Reason                                                                            | Alternative                                                                            |
|---------------------------------|-----------------------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| `aux_source_directory`          | Interacts poorly with incremental builds and Git                                  | List source files explicitly                                                           |
| `build_command`                 | CTest internal function                                                           | Use CTest build-and-test mode via [`CMAKE_CTEST_COMMAND`][cmake_ctest_command]         |
| `cmake_host_system_information` | Usually misleading information.                                                   | Inspect [toolchain][cmake-toolchains] variables and use generator expressions.         |
| `cmake_policy(... OLD)`         | OLD policies are deprecated by definition.                                        | Instead, fix the code to work with the new policy.                                     |
| `create_test_sourcelist`        | We use our own unit testing solution                                              | See the [adding tests](#adding-tests) section.                                         |
| `define_property`               | Adds unnecessary complexity                                                       | Use a cache variable. Exceptions under special circumstances.                          |
| `enable_language`               | Halide is C/C++ only                                                              | [`FindCUDAToolkit`][findcudatoolkit] or [`FindCUDA`][findcuda], appropriately guarded. |
| `file(GLOB ...)`                | Interacts poorly with incremental builds and Git                                  | List source files explicitly. Allowed if not globbing for source files.                |
| `fltk_wrap_ui`                  | Halide does not use FLTK                                                          | None                                                                                   |
| `include_external_msproject`    | Halide must remain portable                                                       | Write a CMake package config file or find module.                                      |
| `include_guard`                 | Use of recursive inclusion is not allowed                                         | Write (recursive) functions.                                                           |
| `include_regular_expression`    | Changes default dependency checking behavior                                      | None                                                                                   |
| `load_cache`                    | Superseded by [`FetchContent`][fetchcontent]/[`ExternalProject`][externalproject] | Use aforementioned modules                                                             |
| `macro`                         | CMake macros are not hygienic and are therefore error-prone                       | Use functions instead.                                                                 |
| `site_name`                     | Privacy: do not want leak host name information                                   | Provide a cache variable, generate a unique name.                                      |
| `variable_watch`                | Debugging helper                                                                  | None. Not needed in production.                                                        |

Lastly, do not introduce any dependencies via [`find_package`][find_package]
without broader approval. Confine dependencies to the `dependencies/` subtree.

### Prohibited variables list

Any variables that are specific to languages that are not enabled should, of
course, be avoided. But of greater concern are variables that are easy to misuse
or should not be overridden for our end-users. The following (non-exhaustive)
list of variables shall not be used in code merged into main.

| Variable                        | Reason                                        | Alternative                                                                                             |
|---------------------------------|-----------------------------------------------|---------------------------------------------------------------------------------------------------------|
| `CMAKE_ROOT`                    | Code smell                                    | Rely on `find_package` search options; include `HINTS` if necessary                                     |
| `CMAKE_DEBUG_TARGET_PROPERTIES` | Debugging helper                              | None                                                                                                    |
| `CMAKE_FIND_DEBUG_MODE`         | Debugging helper                              | None                                                                                                    |
| `CMAKE_RULE_MESSAGES`           | Debugging helper                              | None                                                                                                    |
| `CMAKE_VERBOSE_MAKEFILE`        | Debugging helper                              | None                                                                                                    |
| `CMAKE_BACKWARDS_COMPATIBILITY` | Deprecated                                    | None                                                                                                    |
| `CMAKE_BUILD_TOOL`              | Deprecated                                    | `${CMAKE_COMMAND} --build` or [`CMAKE_MAKE_PROGRAM`][cmake_make_program] (but see below)                |
| `CMAKE_CACHEFILE_DIR`           | Deprecated                                    | [`CMAKE_BINARY_DIR`][cmake_binary_dir], but see below                                                   |
| `CMAKE_CFG_INTDIR`              | Deprecated                                    | `$<CONFIG>`, `$<TARGET_FILE:..>`, target resolution of [`add_custom_command`][add_custom_command], etc. |
| `CMAKE_CL_64`                   | Deprecated                                    | [`CMAKE_SIZEOF_VOID_P`][cmake_sizeof_void_p]                                                            |
| `CMAKE_COMPILER_IS_*`           | Deprecated                                    | [`CMAKE_<LANG>_COMPILER_ID`][cmake_lang_compiler_id]                                                    |
| `CMAKE_HOME_DIRECTORY`          | Deprecated                                    | [`CMAKE_SOURCE_DIR`][cmake_source_dir], but see below                                                   |
| `CMAKE_DIRECTORY_LABELS`        | Directory property                            | None                                                                                                    |
| `CMAKE_BUILD_TYPE`              | Only applies to single-config generators.     | `$<CONFIG>`                                                                                             |
| `CMAKE_*_FLAGS*` (w/o `_INIT`)  | User-only                                     | Write a [toolchain][cmake-toolchains] file with the corresponding `_INIT` variable                      |
| `CMAKE_COLOR_MAKEFILE`          | User-only                                     | None                                                                                                    |
| `CMAKE_ERROR_DEPRECATED`        | User-only                                     | None                                                                                                    |
| `CMAKE_CONFIGURATION_TYPES`     | We only support the four standard build types | None                                                                                                    |

Of course feel free to insert debugging helpers _while developing_ but please
remove them before review. Finally, the following variables are allowed, but
their use must be motivated:

| Variable                                       | Reason                                              | Alternative                                                                                  |
|------------------------------------------------|-----------------------------------------------------|----------------------------------------------------------------------------------------------|
| [`CMAKE_SOURCE_DIR`][cmake_source_dir]         | Points to global source root, not Halide's.         | [`Halide_SOURCE_DIR`][project-name_source_dir] or [`PROJECT_SOURCE_DIR`][project_source_dir] |
| [`CMAKE_BINARY_DIR`][cmake_binary_dir]         | Points to global build root, not Halide's           | [`Halide_BINARY_DIR`][project-name_binary_dir] or [`PROJECT_BINARY_DIR`][project_binary_dir] |
| [`CMAKE_MAKE_PROGRAM`][cmake_make_program]     | CMake abstracts over differences in the build tool. | Prefer CTest's build and test mode or CMake's `--build` mode                                 |
| [`CMAKE_CROSSCOMPILING`][cmake_crosscompiling] | Often misleading.                                   | Inspect relevant variables directly, eg. [`CMAKE_SYSTEM_NAME`][cmake_system_name]            |
| [`BUILD_SHARED_LIBS`][build_shared_libs]       | Could override user setting                         | None, but be careful to restore value when overriding for a dependency                       |

Any use of these functions and variables will block a PR.

## Adding tests

When adding a file to any of the folders under `test`, be aware that CI expects
that every `.c` and `.cpp` appears in the `CMakeLists.txt` file _on its own
line_, possibly as a comment. This is to avoid globbing and also to ensure that
added files are not missed.

For most test types, it should be as simple as adding to the existing lists,
which must remain in alphabetical order. Generator tests are trickier, but
following the existing examples is a safe way to go.

## Adding apps

If you're contributing a new app to Halide: great! Thank you! There are a few
guidelines you should follow when writing a new app.

- Write the app as if it were a top-level project. You should call
  `find_package(Halide)` and set the C++ version to 11.
- Call [`enable_testing()`][enable_testing] and add a small test that runs the
  app.
- Don't assume your app will have access to a GPU. Write your schedules to be
  robust to varying buildbot hardware.
- Don't assume your app will be run on a specific OS, architecture, or bitness.
  Write your apps to be robust (ideally efficient) on all supported platforms.
- If you rely on any additional packages, don't include them as `REQUIRED`,
  instead test to see if their targets are available and, if not, call
  `return()` before creating any targets. In this case, print a
  `message(STATUS "[SKIP] ...")`, too.
- Look at the existing apps for examples.
- Test your app with ctest before opening a PR. Apps are built as part of the
  test, rather than the main build.

[add_custom_command]:
  https://cmake.org/cmake/help/latest/command/add_custom_command.html
[add_library]: https://cmake.org/cmake/help/latest/command/add_library.html
[add_subdirectory]:
  https://cmake.org/cmake/help/latest/command/add_subdirectory.html
[atlas]: http://math-atlas.sourceforge.net/
[brew-cmake]: https://formulae.brew.sh/cask/cmake#default
[build_shared_libs]:
  https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html
[choco-cmake]: https://chocolatey.org/packages/cmake/
[choco-doxygen]: https://chocolatey.org/packages/doxygen.install
[choco-ninja]: https://chocolatey.org/packages/ninja
[chocolatey]: https://chocolatey.org/
[cmake-apt]: https://apt.kitware.com/
[cmake-discourse]: https://discourse.cmake.org/
[cmake-docs]: https://cmake.org/cmake/help/latest/
[cmake-download]: https://cmake.org/download/
[cmake-from-source]: https://cmake.org/install/
[cmake-genex]:
  https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html
[cmake-install]:
  https://cmake.org/cmake/help/latest/manual/cmake.1.html#install-a-project
[cmake-propagation]:
  https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#transitive-usage-requirements
[cmake-toolchains]:
  https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
[cmake-user-interaction]:
  https://cmake.org/cmake/help/latest/guide/user-interaction/index.html#setting-build-variables
[cmake_binary_dir]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_BINARY_DIR.html
[cmake_build_type]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
[cmake_crosscompiling]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CROSSCOMPILING.html
[cmake_crosscompiling_emulator]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CROSSCOMPILING_EMULATOR.html
[cmake_ctest_command]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CTEST_COMMAND.html
[cmake_current_binary_dir]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CURRENT_BINARY_DIR.html
[cmake_cxx_extensions]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_EXTENSIONS.html
[cmake_cxx_standard]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_STANDARD.html
[cmake_cxx_standard_required]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_STANDARD_REQUIRED.html
[cmake_foreach]:
  https://cmake.org/cmake/help/latest/command/foreach.html
[cmake_if]:
  https://cmake.org/cmake/help/latest/command/if.html
[cmake_lang_compiler_id]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER_ID.html
[cmake_make_program]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_MAKE_PROGRAM.html
[cmake_minimum_required]:
  https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html
[cmake_prefix_path]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_PREFIX_PATH.html
[cmake_presets]:
  https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
[cmake_sizeof_void_p]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_SIZEOF_VOID_P.html
[cmake_source_dir]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_SOURCE_DIR.html
[cmake_system_name]:
  https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html
[doxygen-download]: https://www.doxygen.nl/download.html
[doxygen]: https://www.doxygen.nl/index.html
[eigen]: http://eigen.tuxfamily.org/index.php?title=Main_Page
[enable_testing]:
  https://cmake.org/cmake/help/latest/command/enable_testing.html
[externalproject]:
  https://cmake.org/cmake/help/latest/module/ExternalProject.html
[fetchcontent]: https://cmake.org/cmake/help/latest/module/FetchContent.html
[find_package]: https://cmake.org/cmake/help/latest/command/find_package.html
[findcuda]: https://cmake.org/cmake/help/latest/module/FindCUDA.html
[findcudatoolkit]:
  https://cmake.org/cmake/help/latest/module/FindCUDAToolkit.html
[finddoxygen]: https://cmake.org/cmake/help/latest/module/FindDoxygen.html
[findjpeg]: https://cmake.org/cmake/help/latest/module/FindJPEG.html
[findopencl]: https://cmake.org/cmake/help/latest/module/FindOpenCL.html
[findopengl]: https://cmake.org/cmake/help/latest/module/FindOpenGL.html
[findpng]: https://cmake.org/cmake/help/latest/module/FindPNG.html
[findpython3]: https://cmake.org/cmake/help/latest/module/FindPython3.html
[findx11]: https://cmake.org/cmake/help/latest/module/FindX11.html
[halide-generator-tutorial]:
  https://halide-lang.org/tutorials/tutorial_lesson_15_generators.html
[halide-tutorials]: https://halide-lang.org/tutorials/tutorial_introduction.html
[homebrew]: https://brew.sh
[imported-executable]:
  https://cmake.org/cmake/help/latest/command/add_executable.html#imported-executables
[imported-target]:
  https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#imported-targets
[include]: https://cmake.org/cmake/help/latest/command/include.html
[install-files]: https://cmake.org/cmake/help/latest/command/install.html#files
[install-targets]:
  https://cmake.org/cmake/help/latest/command/install.html#targets
[libjpeg]: https://www.libjpeg-turbo.org/
[libpng]: http://www.libpng.org/pub/png/libpng.html
[lld]: https://lld.llvm.org/
[msvc]: https://cmake.org/cmake/help/latest/variable/MSVC.html
[msvc-cmd]:
  https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=vs-2019
[ninja-download]: https://github.com/ninja-build/ninja/releases
[ninja]: https://ninja-build.org/
[openblas]: https://www.openblas.net/
[project]: https://cmake.org/cmake/help/latest/command/project.html
[project-name_binary_dir]:
  https://cmake.org/cmake/help/latest/variable/PROJECT-NAME_BINARY_DIR.html
[project-name_source_dir]:
  https://cmake.org/cmake/help/latest/variable/PROJECT-NAME_SOURCE_DIR.html
[project_source_dir]:
  https://cmake.org/cmake/help/latest/variable/PROJECT_SOURCE_DIR.html
[project_binary_dir]:
  https://cmake.org/cmake/help/latest/variable/PROJECT_BINARY_DIR.html
[pypi-cmake]: https://pypi.org/project/cmake/
[python]: https://www.python.org/downloads/
[target-file]:
  https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#target-dependent-queries
[target_compile_definitions]:
  https://cmake.org/cmake/help/latest/command/target_compile_definitions.html
[target_compile_options]:
  https://cmake.org/cmake/help/latest/command/target_compile_options.html
[target_include_directories]:
  https://cmake.org/cmake/help/latest/command/target_include_directories.html
[target_link_libraries]:
  https://cmake.org/cmake/help/latest/command/target_link_libraries.html
[target_link_options]:
  https://cmake.org/cmake/help/latest/command/target_link_options.html
[vcpkg]: https://github.com/Microsoft/vcpkg
[vcvarsall]:
  https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=vs-2019#vcvarsall-syntax
[venv]: https://docs.python.org/3/tutorial/venv.html
[vs2019-cmake-docs]:
  https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=vs-2019
[win32]: https://cmake.org/cmake/help/latest/variable/WIN32.html
