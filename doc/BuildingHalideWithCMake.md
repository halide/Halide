# Building Halide with CMake

This is a detailed guide to building Halide with CMake. If you want to learn how
to use Halide in your own CMake projects, see [HalideCMakePackage.md]. If you
are looking for Halide's CMake coding guidelines, see [CodeStyleCMake.md].

<!-- TOC -->
* [Building Halide with CMake](#building-halide-with-cmake)
* [Installing CMake](#installing-cmake)
  * [Cross-platform](#cross-platform)
  * [Windows](#windows)
  * [macOS](#macos)
  * [Ubuntu Linux](#ubuntu-linux)
  * [Optional: Install Ninja](#optional-install-ninja)
* [Dependencies](#dependencies)
  * [Summary](#summary)
  * [Installing dependencies](#installing-dependencies)
    * [vcpkg](#vcpkg)
    * [Windows](#windows-1)
    * [Homebrew](#homebrew)
    * [Ubuntu / Debian](#ubuntu--debian)
    * [Python](#python)
* [Building Halide](#building-halide)
  * [Basic build](#basic-build)
    * [Windows](#windows-2)
    * [macOS and Linux](#macos-and-linux)
  * [CMake Presets](#cmake-presets)
    * [Common presets](#common-presets)
    * [Vcpkg presets](#vcpkg-presets)
    * [Sanitizer presets](#sanitizer-presets)
  * [Build options](#build-options)
  * [Installing](#installing)
* [Building Halide with pip](#building-halide-with-pip)
<!-- TOC -->

# Installing CMake

This section covers installing a recent version of CMake and the correct
dependencies for building and using Halide. If you have not used CMake before,
we strongly suggest reading through the [CMake documentation][cmake-docs] first.

Halide requires at least version 3.28. Fortunately, getting a recent version of
CMake couldn't be easier, and there are multiple good options on any system to
do so. Generally, one should always have the most recent version of CMake
installed system-wide. CMake is committed to backwards compatibility and even
the most recent release can build projects over a decade old.

## Cross-platform

Kitware provides packages for CMake on [PyPI][pypi-cmake] which can be installed
via `pip` into a [virtual environment][venv]. There are binary wheels available
for nearly all relevant platforms, including:

| OS                | x86-32             | x86-64             | ARM64                      |
|-------------------|--------------------|--------------------|----------------------------|
| Windows           | :white_check_mark: | :white_check_mark: | :white_check_mark:         |
| macOS             | :x:                | 10.10+             | 11.0+ (incl. `universal2`) |
| Linux (musl 1.1+) | :white_check_mark: | :white_check_mark: | :white_check_mark:         |
| Linux (glibc)     | glibc 2.12+        | glibc 2.12+        | glibc 2.17+                |

We recommend installing CMake using [pipx] to avoid package conflicts and
redundant installations. After installing pipx, run:

```shell
$ pipx install cmake
```

Alternatively, you can use a normal virtual environment:

```shell
$ python -m pip install cmake
```

If you don't want Python to manage your CMake installation, you can either
follow the platform-specific instructions below or install CMake
from [Kitware's binary releases][cmake-download]. If all else fails, you might
need to build CMake from source (e.g. on 32-bit ARM). In that case, follow the
directions posted on [Kitware's website][cmake-from-source].

## Windows

On Windows, there are two primary methods for installing an up-to-date CMake:

1. You can get CMake through the Visual Studio 2022 installer.
2. You can use Windows's built-in package manager, [winget][winget]:
   ```shell
   winget install Kitware.CMake
   ```

We prefer the first option for its simplicity. See
Microsoft's [documentation][vs-cmake-docs] for more details.

## macOS

[Homebrew] keeps its [CMake package][brew-cmake] up to date. Simply run:

```shell
$ brew install cmake
```

## Ubuntu Linux

There are a few good ways to install CMake on Ubuntu:

1. If you're running 24.04 LTS, then simply running
   `sudo apt install cmake` will install CMake 3.28.
2. If you're running an older LTS or would like to use the newest CMake, try
   installing via the [snap store][snap store]: `snap install cmake`. Note this
   will conflict with an APT-provided CMake.
3. Kitware also provides an [APT repository][cmake-apt] with up-to-date
   releases. Compatible with 20.04 LTS+ and is the best option for 32-bit ARM.

For other Linux distributions, check with your distribution's package manager.

**Note:** On WSL 1, snap is not available; in this case, prefer to use APT. On
WSL 2, all methods are available.

## Optional: Install Ninja

We strongly recommend using [Ninja] as your go-to CMake generator for working
with Halide. It has a much richer dependency structure than the alternatives,
and it is the only generator capable of producing accurate incremental builds.

It is available in most package repositories:

* Python: `pipx install ninja`
* Visual Studio Installer: alongside CMake
* winget: `winget install Ninja-build.Ninja`
* Homebrew: `brew install ninja`
* APT: `apt install ninja-build`

You can also place a [pre-built binary][ninja-download] from their website in
the PATH.

# Dependencies

## Summary

The following is a complete list of required and optional dependencies for
building the core pieces of Halide.

| Dependency    | Version            | Required when...           | Notes                                               |
|---------------|--------------------|----------------------------|-----------------------------------------------------|
| [LLVM]        | _see policy below_ | _always_                   | WebAssembly and X86 targets are required.           |
| [Clang]       | `==LLVM`           | _always_                   |                                                     |
| [LLD]         | `==LLVM`           | _always_                   |                                                     |
| [flatbuffers] | `~=23.5.26`        | `WITH_SERIALIZATION=ON`    |                                                     |
| [wabt]        | `==1.0.36`         | `Halide_WASM_BACKEND=wabt` | Does not have a stable API; exact version required. |
| [V8]          | trunk              | `Halide_WASM_BACKEND=V8`   | Difficult to build. See [WebAssembly.md]            |
| [Python]      | `>=3.8`            | `WITH_PYTHON_BINDINGS=ON`  |                                                     |
| [pybind11]    | `~=2.10.4`         | `WITH_PYTHON_BINDINGS=ON`  |                                                     |

Halide maintains the following compatibility policy with LLVM: Halide version
`N` supports LLVM versions `N`, `N-1`, and `N-2`. Our binary distributions
always include the latest `N` patch at time of release. For most users, we
recommend using a pre-packaged binary release of LLVM rather than trying to
build it yourself.

To build the apps, documentation, and tests, an extended set is needed.

| Dependency                      | Required when...                  | Notes                                                                       |
|---------------------------------|-----------------------------------|-----------------------------------------------------------------------------|
| [CUDA Toolkit][FindCUDAToolkit] | building `apps/cuda_mat_mul`      | When compiling Halide pipelines that use CUDA, only the drivers are needed. |
| [Doxygen][FindDoxygen]          | `WITH_DOCS=ON`                    |                                                                             |
| [Eigen3][Eigen3CMake]           | building `apps/linear_algebra`    |                                                                             |
| [libjpeg][FindJPEG]             | `WITH_TESTS=ON`                   | Optionally used by `halide_image_io.h` and `Halide::ImageIO` in CMake.      |
| [libpng][FindPNG]               | `WITH_TESTS=ON`                   | (same as libjpeg)                                                           |
| [BLAS][FindBLAS]                | building `apps/linear_algebra`    | [ATLAS] and [OpenBLAS] are supported implementations                        |
| [OpenCL][FindOpenCL]            | compiling pipelines with `opencl` |                                                                             |

It is best practice to configure your environment so that CMake can find
dependencies without package-specific hints. For instance, if you want CMake to
use a particular version of Python, create a virtual environment and activate it
_before_ configuring Halide. Similarly, the `CMAKE_PREFIX_PATH` variable can be
set to a local directory where from-source dependencies have been installed.
Carefully consult the [find_package] documentation to learn how the search
procedure works.

If the build still fails to find a dependency, each package provides a bespoke
interface for providing hints and overriding incorrect results. Documentation
for these packages is linked in the table above.

## Installing dependencies

### vcpkg

Halide has first-class support for using [vcpkg] to manage dependencies. The
list of dependencies and features is contained inside `vcpkg.json` at the root
of the repository.

By default, a minimum set of LLVM backends will be enabled to compile JIT code
for the host and the serialization feature will be enabled. When using the vcpkg
toolchain file, you can set `-DVCPKG_MANIFEST_FEATURES=developer`
to enable building all dependencies (except Doxygen, which is not available on
vcpkg).

By default, running `vcpkg install` will try to build all of LLVM. This is often
undesirable as it takes very long to do and consumes a lot of disk space,
especially as `vcpkg` requires special configuration to disable the debug build.
It will _also_ attempt to build Python 3 as a dependency of pybind11.

To mitigate this issue, we provide a [vcpkg-overlay] that disables building LLVM
and Python. When using the vcpkg toolchain, you can enable it by setting
`-DVCPKG_OVERLAY_PORTS=cmake/vcpkg`.

If you do choose to use vcpkg to build LLVM (the easiest way on Windows), note
that it is safe to delete the intermediate build files and caches in
`D:\vcpkg\buildtrees` and `%APPDATA%\local\vcpkg`.

For convenience, we provide [CMake presets](#cmake-presets) that set these flags
appropriately per-platform. They are documented further below.

### Windows

On Windows, we recommend using `vcpkg` to install library dependencies.

To build the documentation, you will need to install [Doxygen]. This can be done
either from the [Doxygen website][doxygen-download] or through [winget][winget]:

```shell
$ winget install DimitriVanHeesch.Doxygen
```

To build the Python bindings, you will need to install Python 3. This should be
done by running the official installer from the [Python website][python]. Be
sure to download the debugging symbols through the installer. This will require
using the "Advanced Installation" workflow. Although it is not strictly
necessary, it is convenient to install Python system-wide on Windows (i.e.
`C:\Program Files`) because CMake looks at standard paths and registry keys.
This removes the need to manually set the `PATH`.

Once Python is installed, you can install the Python module dependencies either
globally or in a [virtual environment][venv] by running

```shell
$ python -m pip install -r requirements.txt
```

from the root of the repository.

### Homebrew

On macOS, it is possible to install all dependencies via [Homebrew][homebrew]:

```shell
$ brew install llvm flatbuffers wabt python pybind11 doxygen eigen libpng libjpeg openblas
```

The `llvm` package includes `clang`, `clang-format`, and `lld`, too. To ensure
CMake can find the keg-only dependencies, set the following:

```shell
$ export CMAKE_PREFIX_PATH="/opt/homebrew:/opt/homebrew/opt/llvm:/opt/homebrew/opt/jpeg"
```

### Ubuntu / Debian

On Ubuntu you should install the following packages (this includes the Python
module dependencies):

```
$ sudo apt install clang-tools lld llvm-dev libclang-dev liblld-dev \
    libpng-dev libjpeg-dev libgl-dev python3-dev python3-numpy python3-scipy \
    python3-imageio python3-pybind11 libopenblas-dev libeigen3-dev \ 
    libatlas-base-dev doxygen
```

### Python

When running the Python package, you will need to install additional
dependencies. These are tabulated in `requirements.txt` and may be installed
with:

```shell
$ python -m pip install -U pip "setuptools[core]" wheel
$ python -m pip install -r requirements.txt
```

# Building Halide

## Basic build

These instructions assume that your working directory is the Halide repository
root.

### Windows

If you plan to use the Ninja generator, be sure to launch the developer command
prompt corresponding to your intended environment. Note that whatever your
intended target system (x86, x64, or ARM), you must use the 64-bit _host tools_
because the 32-bit tools run out of memory during the linking step with LLVM.
More information is available from [Microsoft's documentation][msvc-cmd].

You should either open the correct Developer Command Prompt directly or run the
[`vcvarsall.bat`][vcvarsall] script with the correct argument, i.e. one of the
following:

```shell
$ "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
$ "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86
$ "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_arm
```

Then, assuming that vcpkg is installed to `D:\vcpkg`, simply run:

```shell
$ cmake -G Ninja -S . -B build --toolchain D:\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
$ cmake --build .\build
```

Valid values of [`CMAKE_BUILD_TYPE`][cmake_build_type] are `Debug`,
`RelWithDebInfo`, `MinSizeRel`, and `Release`. When using a single-configuration
generator (like Ninja) you must specify a build type in the configuration step.

Otherwise, if you wish to create a Visual Studio based build system, you can
configure with:

```shell
$ cmake -G "Visual Studio 17 2022" -Thost=x64 -A x64 -S . -B build ^
        --toolchain D:\vcpkg\scripts\buildsystems\vcpkg.cmake
$ cmake --build .\build --config Release -j %NUMBER_OF_PROCESSORS%
```

Because the Visual Studio generator is a _multi-config generator_, you don't set
`CMAKE_BUILD_TYPE` at configure-time, but instead pass the configuration to the
build (and test/install) commands with the `--config` flag. More documentation
is available in the [CMake User Interaction Guide][cmake-user-interaction].

The process is similar for 32-bit:

```
> cmake -G "Visual Studio 17 2022" -Thost=x64 -A Win32 -S . -B build ^
        --toolchain D:\vcpkg\scripts\buildsystems\vcpkg.cmake
> cmake --build .\build --config Release -j %NUMBER_OF_PROCESSORS%
```

In both cases, the `-Thost=x64` flag ensures that the correct host tools are
used.

**Note:** due to limitations in MSBuild, incremental builds using the VS
generators will miss dependencies (including changes to headers in the
`src/runtime` folder). We recommend using Ninja for day-to-day development and
use Visual Studio only if you need it for packaging.

### macOS and Linux

The instructions here are straightforward. Assuming your environment is set up
correctly, just run:

```shell
$ cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
```

If you omit `-G Ninja`, a Makefile-based generator will likely be used instead.
In either case, [`CMAKE_BUILD_TYPE`][cmake_build_type] must be set to one of the
standard types: `Debug`, `RelWithDebInfo`, `MinSizeRel`, or `Release`.

## CMake Presets

### Common presets

Halide provides several [presets][cmake_presets] to make the above commands more
convenient. The following CMake preset commands correspond to the longer ones
above.

```shell
$ cmake --preset=win64    # VS 2022 generator, 64-bit build, vcpkg deps
$ cmake --preset=win32    # VS 2022 generator, 32-bit build, vcpkg deps
$ cmake --preset=macOS    # Ninja generator, macOS host build, Homebrew deps
$ cmake --preset=debug    # Debug mode, any single-config generator / compiler
$ cmake --preset=release  # Release mode, any single-config generator / compiler
```

### Vcpkg presets

Halide provides two sets of corresponding vcpkg-enabled presets: _base_ and
_full_.

| Base preset     | Full preset          |
|-----------------|----------------------|
| `win32`         | `win32-vcpkg-full`   |
| `win64`         | `win64-vcpkg-full`   |
| `macOS-vcpkg`   | `macOS-vcpkg-full`   |
| `debug-vcpkg`   | `debug-vcpkg-full`   |
| `release-vcpkg` | `release-vcpkg-full` |

In simple terms, the base presets rely on the system to provide LLVM and Python,
while the full presets delegate this to vcpkg (which consumes a large amount of
hard disk space and time).

The `macOS-vcpkg` preset adds `/opt/homebrew/opt/llvm` to
`CMAKE_PREFIX_PATH`.

### Sanitizer presets

There are also presets to use some Clang sanitizers with the CMake build; at
present, only Fuzzer and ASAN (Address Sanitizer) are supported, and only on
linux-x86-64.

* `linux-x64-asan`: Use the Address Sanitizer
* `linux-x64-fuzzer`: Use the Clang fuzzer plugin

To use these, you must build LLVM with additional options:

```
-DLLVM_ENABLE_PROJECTS="clang;lld;clang-tools-extra"
-DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind"
```

## Build options

Halide reads and understands several options that can configure the build. The
following are the most consequential and control how Halide is actually
compiled.

| Option                                   | Default               | Description                                                                                       |
|------------------------------------------|-----------------------|---------------------------------------------------------------------------------------------------|
| [`BUILD_SHARED_LIBS`][build_shared_libs] | `ON`                  | Standard CMake variable that chooses whether to build as a static or shared library.              |
| `Halide_BUNDLE_STATIC`                   | `OFF`                 | When building Halide as a static library, merge static library dependencies into libHalide.a.     |
| `Halide_LLVM_SHARED_LIBS`                | `OFF`                 | Link to the shared version of LLVM. Not available on Windows.                                     |
| `Halide_ENABLE_RTTI`                     | _inherited from LLVM_ | Enable RTTI when building Halide. Recommended to be set to `ON`                                   |
| `Halide_ENABLE_EXCEPTIONS`               | `ON`                  | Enable exceptions when building Halide                                                            |
| `Halide_TARGET`                          | _empty_               | The default target triple to use for `add_halide_library` (and the generator tests, by extension) |
| `WITH_AUTOSCHEDULERS`                    | `ON`                  | Enable building the autoschedulers. Requires `BUILD_SHARED_LIBS`.                                 |
| `WITH_SERIALIZATION`                     | `ON`                  | Include experimental Serialization/Deserialization features                                       |

The following options are disabled by default when building Halide through the [
`add_subdirectory`][add_subdirectory]
or [`FetchContent`][fetchcontent] mechanisms. They control whether non-essential
targets (like tests and documentation) are built.

| Option                 | Default | Description                                                      |
|------------------------|---------|------------------------------------------------------------------|
| `WITH_DOCS`            | `OFF`   | Enable building the documentation via Doxygen                    |
| `WITH_PACKAGING`       | `ON`    | Include the `install()` rules for Halide.                        |
| `WITH_PYTHON_BINDINGS` | `ON`    | Enable building Python 3 bindings                                |
| `WITH_TESTS`           | `ON`    | Enable building unit and integration tests                       |
| `WITH_TUTORIALS`       | `ON`    | Enable building the tutorials                                    |
| `WITH_UTILS`           | `ON`    | Enable building various utilities including the trace visualizer |

The following options are _advanced_ and should not be required in typical
workflows. Generally, these are used by Halide's own CI infrastructure, or as
escape hatches for third-party packagers.

| Option                      | Default                                                            | Description                                                                              |
|-----------------------------|--------------------------------------------------------------------|------------------------------------------------------------------------------------------|
| `Halide_CLANG_TIDY_BUILD`   | `OFF`                                                              | Used internally to generate fake compile jobs for runtime files when running clang-tidy. |
| `Halide_CCACHE_BUILD`       | `OFF`                                                              | Use ccache with Halide-recommended settings to accelerate rebuilds.                      |
| `Halide_CCACHE_PARAMS`      | `CCACHE_CPP2=yes CCACHE_HASHDIR=yes CCACHE_SLOPPINESS=pch_defines` | Options to pass to `ccache` when using `Halide_CCACHE_BUILD`.                            |
| `Halide_VERSION_OVERRIDE`   | `${Halide_VERSION}`                                                | Override the VERSION for libHalide.                                                      |
| `Halide_SOVERSION_OVERRIDE` | `${Halide_VERSION_MAJOR}`                                          | Override the SOVERSION for libHalide. Expects a positive integer (i.e. not a version).   |

The following options control whether to build certain test subsets. They only
apply when `WITH_TESTS=ON`:

| Option                    | Default    | Description                           |
|---------------------------|------------|---------------------------------------|
| `WITH_TEST_AUTO_SCHEDULE` | `ON`       | enable the auto-scheduling tests      |
| `WITH_TEST_CORRECTNESS`   | `ON`       | enable the correctness tests          |
| `WITH_TEST_ERROR`         | `ON`       | enable the expected-error tests       |
| `WITH_TEST_FUZZ`          | _detected_ | enable the libfuzzer-based fuzz tests |
| `WITH_TEST_GENERATOR`     | `ON`       | enable the AOT generator tests        |
| `WITH_TEST_PERFORMANCE`   | `ON`       | enable performance testing            |
| `WITH_TEST_RUNTIME`       | `ON`       | enable testing the runtime modules    |
| `WITH_TEST_WARNING`       | `ON`       | enable the expected-warning tests     |

The following option selects the execution engine for in-process WASM testing:

| Option                | Default | Description                                                                              |
|-----------------------|---------|------------------------------------------------------------------------------------------|
| `Halide_WASM_BACKEND` | `wabt`  | Select the backend for WASM testing. Can be `wabt`, `V8` or a false value such as `OFF`. |

## Installing

Once built, Halide will need to be installed somewhere before using it in a
separate project. On any platform, this means running the
[`cmake --install`][cmake-install] command in one of two ways. For a
single-configuration generator (like Ninja), run either:

```shell
$ cmake --install ./build --prefix /path/to/Halide-install
$ cmake --install .\build --prefix X:\path\to\Halide-install
```

For a multi-configuration generator (like Visual Studio) run:

```shell
$ cmake --install ./build --prefix /path/to/Halide-install --config Release
$ cmake --install .\build --prefix X:\path\to\Halide-install --config Release
```

Of course, make sure that you build the corresponding config before attempting
to install it.

# Building Halide with pip

Halide also supports installation via the standard Python packaging workflow.
Running `pip install .` at the root of the repository will build a wheel and
install it into the currently active Python environment.

However, this comes with a few caveats:

1. `Halide_USE_FETCHCONTENT` is disabled, so the environment must be prepared
   for CMake to find its dependencies. This is easiest to do by setting either
   `CMAKE_PREFIX_PATH` to pre-built dependencies or by setting
   `CMAKE_TOOLCHAIN_FILE` to vcpkg.
2. The build settings are fixed, meaning that `wabt` is required on non-Windows
   systems, `flatbuffers` is always required, and the Python bindings must be
   built.
3. The generated wheel will likely only work on your system. In particular, it
   will not be repaired with `auditwheel` or `delocate`.

Even so, this is a very good method of installing Halide. It supports both
Python and C++ `find_package` workflows.


[ATLAS]: http://math-atlas.sourceforge.net/

[BuildingHalideWithCMake.md]: ./BuildingHalideWithCMake.md

[Clang]: https://clang.llvm.org

[CodeStyleCMake.md]: ./CodeStyleCMake.md

[Eigen3CMake]: https://eigen.tuxfamily.org/dox/TopicCMakeGuide.html

[Eigen3]: http://eigen.tuxfamily.org/index.php?title=Main_Page

[FindBLAS]: https://cmake.org/cmake/help/latest/module/FindBLAS.html

[FindCUDAToolkit]: https://cmake.org/cmake/help/latest/module/FindCUDAToolkit.html

[FindCUDA]: https://cmake.org/cmake/help/latest/module/FindCUDA.html

[FindDoxygen]: https://cmake.org/cmake/help/latest/module/FindDoxygen.html

[FindJPEG]: https://cmake.org/cmake/help/latest/module/FindJPEG.html

[FindOpenCL]: https://cmake.org/cmake/help/latest/module/FindOpenCL.html

[FindPNG]: https://cmake.org/cmake/help/latest/module/FindPNG.html

[FindPython]: https://cmake.org/cmake/help/latest/module/FindPython.html

[HalideCMakePackage.md]: ./HalideCMakePackage.md

[LLVM]: https://github.com/llvm/llvm-project

[Ninja]: https://ninja-build.org/

[OpenBLAS]: https://www.openblas.net/

[V8]: https://v8.dev

[WebAssembly.md]: ./WebAssembly.md

[add_subdirectory]: https://cmake.org/cmake/help/latest/command/add_subdirectory.html

[brew-cmake]: https://formulae.brew.sh/cask/cmake#default

[build_shared_libs]: https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html

[cmake-apt]: https://apt.kitware.com/

[cmake-docs]: https://cmake.org/cmake/help/latest/

[cmake-download]: https://cmake.org/download/

[cmake-from-source]: https://cmake.org/install/

[cmake-install]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#install-a-project

[cmake-user-interaction]: https://cmake.org/cmake/help/latest/guide/user-interaction/index.html#setting-build-variables

[cmake_build_type]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html

[cmake_presets]: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html

[doxygen-download]: https://www.doxygen.nl/download.html

[doxygen]: https://www.doxygen.nl/index.html

[enable_testing]: https://cmake.org/cmake/help/latest/command/enable_testing.html

[fetchcontent]: https://cmake.org/cmake/help/latest/module/FetchContent.html

[find_package]: https://cmake.org/cmake/help/latest/command/find_package.html

[flatbuffers]: https://github.com/google/flatbuffers

[homebrew]: https://brew.sh

[libjpeg]: https://www.libjpeg-turbo.org/

[libpng]: http://www.libpng.org/pub/png/libpng.html

[lld]: https://lld.llvm.org/

[msvc-cmd]: https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line

[ninja-download]: https://github.com/ninja-build/ninja/releases

[pipx]: https://pipx.pypa.io/stable/

[pybind11]: https://github.com/pybind/pybind11

[pypi-cmake]: https://pypi.org/project/cmake/

[python]: https://www.python.org/downloads/

[snap store]: https://snapcraft.io/cmake

[vcpkg-overlay]: https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports

[vcpkg]: https://github.com/Microsoft/vcpkg

[vcvarsall]: https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line#developer_command_file_locations

[venv]: https://docs.python.org/3/tutorial/venv.html

[vs-cmake-docs]: https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio

[wabt]: https://github.com/WebAssembly/wabt

[winget]: https://learn.microsoft.com/en-us/windows/package-manager/winget/
