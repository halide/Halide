# Using Halide from your CMake build

This is a detailed guide to building your own Halide programs with the official
CMake package. If you need directions for building Halide,
see [BuildingHalideWithCMake.md]. If you are looking for Halide's CMake coding
guidelines, see [CodeStyleCMake.md].

This document assumes some basic familiarity with CMake but tries to be explicit
in all its examples. To learn more about CMake, consult the
[documentation][cmake-docs] and engage with the community on
the [CMake Discourse][cmake-discourse].

<!-- TOC -->
* [Using Halide from your CMake build](#using-halide-from-your-cmake-build)
* [A basic CMake project](#a-basic-cmake-project)
  * [JIT mode](#jit-mode)
  * [AOT mode](#aot-mode)
    * [Autoschedulers](#autoschedulers)
    * [RunGenMain](#rungenmain)
* [Halide package documentation](#halide-package-documentation)
  * [Components](#components)
  * [Variables](#variables)
  * [Imported targets](#imported-targets)
  * [Functions](#functions)
    * [`add_halide_generator`](#add_halide_generator)
    * [`add_halide_library`](#add_halide_library)
    * [`add_halide_python_extension_library`](#add_halide_python_extension_library)
    * [`add_halide_runtime`](#add_halide_runtime)
* [Cross compiling](#cross-compiling)
  * [Use `add_halide_generator`](#use-add_halide_generator)
  * [Use a super-build](#use-a-super-build)
  * [Use `ExternalProject` directly](#use-externalproject-directly)
  * [Use an emulator or run on device](#use-an-emulator-or-run-on-device)
  * [Bypass CMake](#bypass-cmake)
<!-- TOC -->

# A basic CMake project

There are two main ways to use Halide in your application: as a **JIT compiler**
for dynamic pipelines or an **ahead-of-time (AOT) compiler** for static
pipelines. CMake provides robust support for both use cases.

No matter how you intend to use Halide, you will need some basic CMake
boilerplate.

```cmake
cmake_minimum_required(VERSION 3.28)
project(HalideExample)

set(CMAKE_CXX_STANDARD 17)  # or newer
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)
```

The [`cmake_minimum_required`][cmake_minimum_required] command is required to be
the first command executed in a CMake program. It disables all the deprecated
behavior ("policies" in CMake lingo) from earlier versions. The
[`project`][project] command sets the name of the project (and accepts arguments
for versioning, language support, etc.) and is required by CMake to be called
immediately after setting the minimum version.

The next three variables set the project-wide C++ standard. The first,
[`CMAKE_CXX_STANDARD`][cmake_cxx_standard], simply sets the standard version.
Halide requires at least C++17. The second,
[`CMAKE_CXX_STANDARD_REQUIRED`][cmake_cxx_standard_required], tells CMake to
fail if the compiler cannot provide the requested standard version. Lastly,
[`CMAKE_CXX_EXTENSIONS`][cmake_cxx_extensions] tells CMake to disable
vendor-specific extensions to C++. This is not necessary to simply use Halide,
but we do not allow such extensions in the Halide repo.

Finally, we use [`find_package`][find_package] to locate Halide on your system.
When using the pip package on Linux and macOS, CMake's `find_package`
command should find Halide as long as you're in the same virtual environment you
installed it in. On Windows, you will need to add the virtual environment root
directory to [`CMAKE_PREFIX_PATH`][cmake_prefix_path]:

```shell
$ cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%VIRTUAL_ENV%
```

If `find_package` cannot find Halide, set `CMAKE_PREFIX_PATH` to the Halide
installation directory.

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
re-read the [tutorial on generators][halide-generator-tutorial]. Assume (like in
the tutorial) that you have a source file named `my_generators.cpp` and that in
it, you have generator classes `MyFirstGenerator` and `MySecondGenerator` with
registered names `my_first_generator` and `my_second_generator` respectively.

Then the first step is to add a **generator executable** to your build:

```cmake
# ... same project setup as before ...
add_halide_generator(my_generators SOURCES my_generators.cpp)
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
executable. Learn how to interact with these executables
in [RunGen.md](./RunGen.md).

# Halide package documentation

Halide provides a CMake _package configuration_ module. The intended way to use
the CMake build is to run `find_package(Halide ...)` in your `CMakeLists.txt`
file. Closely read the [`find_package` documentation][find_package] before
proceeding.

## Components

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

To ensure that the Python bindings are available, include the `Python`
component.

## Variables

Variables that control package loading:

| Variable                    | Description                                                                                                                                                                   |
|-----------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Halide_SHARED_LIBS`        | override `BUILD_SHARED_LIBS` when loading the Halide package via `find_package`. Has no effect when using Halide via `add_subdirectory` as a Git or `FetchContent` submodule. |
| `Halide_RUNTIME_NO_THREADS` | skip linking of Threads library to runtime. Should be set if your toolchain does not support it (e.g. baremetal).                                                             |
| `Halide_RUNTIME_NO_DL_LIBS` | skip linking of DL library to runtime. Should be set if your toolchain does not support it (e.g. baremetal).                                                                  |

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
| `WITH_AUTOSCHEDULERS`      | Whether the autoschedulers are available                           |

Variables that control package behavior:

| Variable                  | Description                                                                                                                                     |
|---------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| `Halide_PYTHON_LAUNCHER`  | Semicolon separated list containing a command to launch the Python interpreter. Can be used to set environment variables for Python generators. |
| `Halide_NO_DEFAULT_FLAGS` | Off by default. When enabled, suppresses recommended compiler flags that would be added by `add_halide_generator`                               |

## Imported targets

Halide defines the following targets that are available to users:

| Imported target      | Description                                                                                                                                                                                    |
|----------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Halide::Halide`     | this is the JIT-mode library to use when using Halide from C++.                                                                                                                                |
| `Halide::Generator`  | this is the target to use when manually defining a generator executable. It supplies a `main()` function.                                                                                      |
| `Halide::Runtime`    | adds include paths to the Halide runtime headers                                                                                                                                               |
| `Halide::Tools`      | adds include paths to the Halide tools, including the benchmarking utility.                                                                                                                    |
| `Halide::ImageIO`    | adds include paths to the Halide image IO utility. Depends on `PNG::PNG` and `JPEG::JPEG` if they exist or were loaded through the corresponding package components.                           |
| `Halide::ThreadPool` | adds include paths to the Halide _simple_ thread pool utility library. This is not the same as the runtime's thread pool and is intended only for use by tests. Depends on `Threads::Threads`. |
| `Halide::RunGenMain` | used with the `REGISTRATION` parameter of `add_halide_library` to create simple runners and benchmarking tools for Halide libraries.                                                           |

The following targets only guaranteed when requesting the `Python` component
(`Halide_Python_FOUND` will be true):

| Imported target  | Description                                                                                                                                                       |
|------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Halide::Python` | this is a Python 3 package that can be referenced as `$<TARGET_FILE_DIR:Halide::Python>/..` when setting up `PYTHONPATH` for Python tests or the like from CMake. |

The following targets only guaranteed when `WITH_AUTOSCHEDULERS` is true:

| Imported target         | Description                                                     |
|-------------------------|-----------------------------------------------------------------|
| `Halide::Adams2019`     | the Adams et.al. 2019 autoscheduler (no GPU support)            |
| `Halide::Anderson2021`  | the Anderson, et.al. 2021 autoscheduler (full GPU support)      |
| `Halide::Li2018`        | the Li et.al. 2018 gradient autoscheduler (limited GPU support) |
| `Halide::Mullapudi2016` | the Mullapudi et.al. 2016 autoscheduler (no GPU support)        |

## Functions

The Halide package provides several useful functions for dealing with AOT
compilation steps.

### `add_halide_generator`

This function aids in creating cross-compilable builds that use Halide
generators.

```
add_halide_generator(
    target
    [PACKAGE_NAME package-name]
    [PACKAGE_NAMESPACE namespace]
    [EXPORT_FILE export-file]
    [PYSTUB generator-name]
    [LINK_LIBRARIES lib1 ...]
    [[SOURCES] source1 ...]
)
```

Every named argument is optional, and the function uses the following default
arguments:

- If `PACKAGE_NAME` is not provided, it defaults to
  `${PROJECT_NAME}-halide_generators`.
- If `PACKAGE_NAMESPACE` is not provided, it defaults to
  `${PROJECT_NAME}::halide_generators::`.
- If `EXPORT_FILE` is not provided, it defaults to
  `${PROJECT_BINARY_DIR}/cmake/${ARG_PACKAGE_NAME}-config.cmake`

This function guarantees that a Halide generator target named
`<namespace><target>` is available. It will first search for a package named
`<package-name>` using `find_package`; if it is found, it is assumed that it
provides the target. Otherwise, it will create an executable target named
`target` and an `ALIAS` target `<namespace><target>`. This function also creates
a custom target named `<package-name>` if it does not exist and
`<target>` would exist. In this case, `<package-name>` will depend on
`<target>`, this enables easy building of _just_ the Halide generators managed
by this function.

After the call, `<PACKAGE_NAME>_FOUND` will be set to true if the host
generators were imported (and hence won't be built). Otherwise, it will be set
to false. This variable may be used to conditionally set properties on
`<target>`.

Please
see [test/integration/xc](https://github.com/halide/Halide/tree/main/test/integration/xc)
for a simple example
and [apps/hannk](https://github.com/halide/Halide/tree/main/apps/hannk) for a
complete app that uses it extensively.

The `SOURCES` keyword marks the beginning of sources to be used to build
`<target>`, if it is not loaded. All unparsed arguments will be interpreted as
sources.

The `LINK_LIBRARIES` argument lists libraries that should be linked to
`<target>` when it is being built in the present build system.

If `PYSTUB` is specified, then a Python Extension will be built that wraps the
Generator with CPython glue to allow use of the Generator Python 3. The result
will be a shared library of the form
`<target>_pystub.<soabi>.so`, where `<soabi>` describes the specific Python
version and platform (e.g., `cpython-310-darwin` for Python 3.10 on macOS). See
[Python.md](Python.md) for examples of use.

### `add_halide_library`

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
                   [FEATURES[<triple>] feature1 [feature2 ...]]
                   [PLUGINS plugin1 [plugin2 ...]]
                   [AUTOSCHEDULER scheduler-name]
                   [FUNCTION_INFO_HEADER OUTVAR]
                   [HEADER OUTVAR]
                   [REGISTRATION OUTVAR]
                   [<extra-output> OUTVAR]
                   [GRADIENT_DESCENT]
                   [C_BACKEND]
                   [NO_THREADS]
                   [NO_DL_LIBS])

triple = <arch>-<bits>-<os>
arch   = x86 | arm | powerpc | hexagon | wasm | riscv
bits   = 32 | 64
os     = linux | windows | osx | android | ios | qurt | noos | fuchsia | wasmrt

extra-output = ASSEMBLY | BITCODE | COMPILER_LOG | C_SOURCE | FEATURIZATION
             | HLPIPE | LLVM_ASSEMBLY | PYTHON_EXTENSION | PYTORCH_WRAPPER
             | SCHEDULE | STMT | STMT_HTML
```

This function creates a called `<target>` corresponding to running the
`<generator-target>` (an executable target which links to `Halide::Generator`)
one time, using command line arguments derived from the other parameters.

The arguments `GENERATOR` and `FUNCTION_NAME` default to `<target>`. They
correspond to the `-g` and `-f` command line flags, respectively.

`NAMESPACE` is syntactic sugar to specify the C++ namespace (if any) of the
generated function; you can also specify the C++ namespace (if any) directly in
the `FUNCTION_NAME` argument, but for repeated declarations or very long
namespaces, specifying this separately can provide more readable build files.

If `USE_RUNTIME` is not specified, this function will create another target
called `<target>.runtime` which corresponds to running the generator with `-r`
and a compatible list of targets. This runtime target is an `INTERFACE`
dependency of `<target>`. If multiple runtime targets need to be linked
together, setting `USE_RUNTIME` to another Halide runtime library, `<target2>`
will prevent the generation of `<target>.runtime` and instead use
`<target2>.runtime`. This argument is most commonly used in conjunction with [
`add_halide_runtime`](#add_halide_runtime).

Parameters can be passed to a generator via the `PARAMS` argument. Parameters
should be space-separated. Similarly, `TARGETS` is a space-separated list of
targets for which to generate code in a single function. They must all share the
same platform/bits/os triple (e.g. `arm-32-linux`). Features that are in common
among all targets, including device libraries (like `cuda`) should go in
`FEATURES`. If `TARGETS` is not specified, the value of `Halide_TARGET`
specified at configure time will be used.

Every element of `TARGETS` must begin with the same `arch-bits-os` triple. This
function understands two _meta-triples_, `host` and `cmake`. The meta-triple
`host` is equal to the `arch-bits-os` triple used to compile Halide along with
all the supported instruction set extensions. On platforms that support running
both 32 and 64-bit programs, this will not necessarily equal the platform the
compiler is running on or that CMake is targeting.

The meta-triple `cmake` is equal to the `arch-bits-os` of the current CMake
target. This is useful if you want to make sure you are not unintentionally
cross-compiling, which would result in an [`IMPORTED` target][imported-target]
being created. When `TARGETS` is empty and the `host` target would not
cross-compile, then `host` will be used. Otherwise, `cmake` will be used and an
author warning will be issued.

When `CMAKE_OSX_ARCHITECTURES` is set and the `TARGETS` argument resolves to
`cmake`, the generator will be run once for each architecture and the results
will be fused together using `lipo`. This behavior extends to runtime targets.

To use an autoscheduler, set the `AUTOSCHEDULER` argument to a target named like
`Namespace::Scheduler`, for example `Halide::Adams2019`. This will set the
`autoscheduler` GeneratorParam on the generator command line to `Scheduler`
and add the target to the list of plugins. Additional plugins can be loaded by
setting the `PLUGINS` argument. If the argument to `AUTOSCHEDULER` does not
contain `::` or it does not name a target, it will be passed to the `-s` flag
verbatim.

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
`CMAKE_CURRENT_BINARY_DIR`) to the generated `.function_info.h` header file will
be set in `OUTVAR`. This produces a file that contains `constexpr`
descriptions of information about the generated functions (e.g., argument type
and information). It is generated separately from the normal `HEADER`
file because `HEADER` is intended to work with basic `extern "C"` linkage, while
`FUNCTION_INFO_HEADER` requires C++17 or later to use effectively.
(This can be quite useful for advanced usages, such as producing automatic call
wrappers, etc.) Examples of usage can be found in the generated file.

Each of the `extra-output` arguments directly correspond to an extra output (via
`-e`) from the generator. The value `OUTVAR` names a variable into which a
path (relative to
[`CMAKE_CURRENT_BINARY_DIR`][cmake_current_binary_dir]) to the extra file will
be written.

When `NO_THREADS` is passed, the library targets will not depend on
`Threads::Threads`. It is your responsibility to link to an equivalent target.

When `NO_DL_LIBS` is passed, the library targets will not depend on
`${CMAKE_DL_LIBS}`. It is your responsibility to link to an equivalent library.

### `add_halide_python_extension_library`

This function wraps the outputs of one or more `add_halide_library` targets with
glue code to produce a Python Extension library.

```
add_halide_python_extension_library(
    target
    [MODULE_NAME module-name]
    HALIDE_LIBRARIES library1 ...
)
```

`HALIDE_LIBRARIES` is a list of one of more `add_halide_library` targets. Each
will be added to the extension as a callable method of the module. Note that
every library specified must be built with the `PYTHON_EXTENSION` keyword
specified, and all libraries must use the same Halide runtime.

The result will be a shared library of the form `<target>.<soabi>.so`, where 
`<soabi>` describes the specific Python version and platform (e.g., 
`cpython-310-darwin` for Python 3.10 on macOS.)

### `add_halide_runtime`

This function generates a library containing a Halide runtime. Most user code
will never need to use this, as `add_halide_library()` will call it for you if
necessary. The most common use case is usually in conjunction with
`add_halide_python_extension_library()`, as a way to ensure that all the halide
libraries share an identical runtime.

```
add_halide_runtime(
    target
    [TARGETS target1 [target2 ...]]
    [NO_THREADS]
    [NO_DL_LIBS]
)
```

The `TARGETS`, `NO_THREADS`, and `NO_DL_LIBS` arguments have identical semantics
to the argument of the same name for [
`add_halide_library`](#add_halide_library).

# Cross compiling

Cross-compiling in CMake can be tricky, since CMake doesn't easily support
compiling for both the host platform and the cross platform within the same
build. Unfortunately, Halide generator executables are just about always
designed to run on the host platform. Each project will be set up differently
and have different requirements, but here are some suggestions for effective use
of CMake in these scenarios.

## Use `add_halide_generator`

If you are writing new programs that use Halide, you might wish to use
`add_halide_generator`. When using this helper, you are expected to build your
project twice: once for your build host and again for your intended target.

When building the host build, you can use the `<package-name>` (see the
documentation above) target to build _just_ the generators. Then, in the target
build, set `<package-name>_ROOT` to the host build directory.

For example:

```
$ cmake -G Ninja -S . -B build-host -DCMAKE_BUILD_TYPE=Release
$ cmake --build build-host --target <package-name>
$ cmake -G Ninja -S . -B build-target --toolchain /path/to/target-tc.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -D<package-name>_ROOT:FILEPATH=$PWD/build-host
$ cmake --build build-target
```

## Use a super-build

A CMake super-build consists of breaking down a project into subprojects that
are isolated by [toolchain][cmake-toolchains]. The basic structure is to have an
outermost project that only coordinates the sub-builds via the
[`ExternalProject`][ExternalProject] module.

One would then use Halide to build a generator executable in one self-contained
project, then export that target to be used in a separate project. The second
project would be configured with the target [toolchain][cmake-toolchains] and
would call `add_halide_library` with no `TARGETS` option and set `FROM` equal to
the name of the imported generator executable. Obviously, this is a significant
increase in complexity over a typical CMake project.

This is very compatible with the `add_halide_generator` strategy above.

## Use `ExternalProject` directly

A lighter weight alternative to the above is to use
[`ExternalProject`][ExternalProject] directly in your parent build. Configure
the parent build with the target [toolchain][cmake-toolchains], and configure
the inner project to use the host toolchain. Then, manually create an
[`IMPORTED` target][imported-executable] for your generator executable and call
`add_halide_library` as described above.

The main drawback of this approach is that creating accurate `IMPORTED` targets
is difficult since predicting the names and locations of your binaries across
all possible platform and CMake project generators is difficult. In particular,
it is hard to predict executable extensions in cross-OS builds.

## Use an emulator or run on device

The [`CMAKE_CROSSCOMPILING_EMULATOR`][cmake_crosscompiling_emulator] variable
allows one to specify a command _prefix_ to run a target-system binary on the
host machine. One could set this to a custom shell script that uploads the
generator executable, runs it on the device and copies back the results.

Another option is to install `qemu-user-static` to transparently emulate the
cross-built generator.

## Bypass CMake

The previous two options ensure that the targets generated by
`add_halide_library` will be _normal_ static libraries. This approach does not
use [`ExternalProject`][ExternalProject], but instead produces `IMPORTED`
targets. The main drawback of `IMPORTED` targets is that they are considered
second-class in CMake. In particular, they cannot be installed with the typical
[`install(TARGETS)` command][install-targets]. Instead, they must be installed
using [`install(FILES)`][install-files] and the
[`$<TARGET_FILE:tgt>`][target-file] generator expression.


[BuildingHalideWithCMake.md]: ./BuildingHalideWithCMake.md

[CodeStyleCMake.md]: ./CodeStyleCMake.md

[ExternalProject]: https://cmake.org/cmake/help/latest/module/ExternalProject.html

[HalideCMakePackage.md]: ./HalideCMakePackage.md

[add_custom_command]: https://cmake.org/cmake/help/latest/command/add_custom_command.html

[add_library]: https://cmake.org/cmake/help/latest/command/add_library.html

[add_subdirectory]: https://cmake.org/cmake/help/latest/command/add_subdirectory.html

[atlas]: http://math-atlas.sourceforge.net/

[brew-cmake]: https://formulae.brew.sh/cask/cmake#default

[build_shared_libs]: https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html

[cmake-apt]: https://apt.kitware.com/

[cmake-discourse]: https://discourse.cmake.org/

[cmake-docs]: https://cmake.org/cmake/help/latest/

[cmake-download]: https://cmake.org/download/

[cmake-from-source]: https://cmake.org/install/

[cmake-genex]: https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html

[cmake-install]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#install-a-project

[cmake-propagation]: https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#transitive-usage-requirements

[cmake-toolchains]: https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html

[cmake-user-interaction]: https://cmake.org/cmake/help/latest/guide/user-interaction/index.html#setting-build-variables

[cmake_binary_dir]: https://cmake.org/cmake/help/latest/variable/CMAKE_BINARY_DIR.html

[cmake_build_type]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html

[cmake_crosscompiling]: https://cmake.org/cmake/help/latest/variable/CMAKE_CROSSCOMPILING.html

[cmake_crosscompiling_emulator]: https://cmake.org/cmake/help/latest/variable/CMAKE_CROSSCOMPILING_EMULATOR.html

[cmake_ctest_command]: https://cmake.org/cmake/help/latest/variable/CMAKE_CTEST_COMMAND.html

[cmake_current_binary_dir]: https://cmake.org/cmake/help/latest/variable/CMAKE_CURRENT_BINARY_DIR.html

[cmake_cxx_extensions]: https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_EXTENSIONS.html

[cmake_cxx_standard]: https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_STANDARD.html

[cmake_cxx_standard_required]: https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_STANDARD_REQUIRED.html

[cmake_foreach]: https://cmake.org/cmake/help/latest/command/foreach.html

[cmake_if]: https://cmake.org/cmake/help/latest/command/if.html

[cmake_lang_compiler_id]: https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER_ID.html

[cmake_make_program]: https://cmake.org/cmake/help/latest/variable/CMAKE_MAKE_PROGRAM.html

[cmake_minimum_required]: https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html

[cmake_prefix_path]: https://cmake.org/cmake/help/latest/variable/CMAKE_PREFIX_PATH.html

[cmake_presets]: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html

[cmake_sizeof_void_p]: https://cmake.org/cmake/help/latest/variable/CMAKE_SIZEOF_VOID_P.html

[cmake_source_dir]: https://cmake.org/cmake/help/latest/variable/CMAKE_SOURCE_DIR.html

[cmake_system_name]: https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html

[doxygen-download]: https://www.doxygen.nl/download.html

[doxygen]: https://www.doxygen.nl/index.html

[eigen]: http://eigen.tuxfamily.org/index.php?title=Main_Page

[enable_testing]: https://cmake.org/cmake/help/latest/command/enable_testing.html

[fetchcontent]: https://cmake.org/cmake/help/latest/module/FetchContent.html

[find_package]: https://cmake.org/cmake/help/latest/command/find_package.html

[findcuda]: https://cmake.org/cmake/help/latest/module/FindCUDA.html

[findcudatoolkit]: https://cmake.org/cmake/help/latest/module/FindCUDAToolkit.html

[finddoxygen]: https://cmake.org/cmake/help/latest/module/FindDoxygen.html

[findjpeg]: https://cmake.org/cmake/help/latest/module/FindJPEG.html

[findopencl]: https://cmake.org/cmake/help/latest/module/FindOpenCL.html

[findpng]: https://cmake.org/cmake/help/latest/module/FindPNG.html

[findpython3]: https://cmake.org/cmake/help/latest/module/FindPython3.html

[findx11]: https://cmake.org/cmake/help/latest/module/FindX11.html

[halide-generator-tutorial]: https://halide-lang.org/tutorials/tutorial_lesson_15_generators.html

[halide-tutorials]: https://halide-lang.org/tutorials/tutorial_introduction.html

[homebrew]: https://brew.sh

[imported-executable]: https://cmake.org/cmake/help/latest/command/add_executable.html#imported-executables

[imported-target]: https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#imported-targets

[include]: https://cmake.org/cmake/help/latest/command/include.html

[install-files]: https://cmake.org/cmake/help/latest/command/install.html#files

[install-targets]: https://cmake.org/cmake/help/latest/command/install.html#targets

[libjpeg]: https://www.libjpeg-turbo.org/

[libpng]: http://www.libpng.org/pub/png/libpng.html

[lld]: https://lld.llvm.org/

[msvc-cmd]: https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line

[msvc]: https://cmake.org/cmake/help/latest/variable/MSVC.html

[ninja-download]: https://github.com/ninja-build/ninja/releases

[ninja]: https://ninja-build.org/

[openblas]: https://www.openblas.net/

[project-name_binary_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT-NAME_BINARY_DIR.html

[project-name_source_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT-NAME_SOURCE_DIR.html

[project]: https://cmake.org/cmake/help/latest/command/project.html

[project_binary_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT_BINARY_DIR.html

[project_source_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT_SOURCE_DIR.html

[pypi-cmake]: https://pypi.org/project/cmake/

[python]: https://www.python.org/downloads/

[target-file]: https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#target-dependent-queries

[target_compile_definitions]: https://cmake.org/cmake/help/latest/command/target_compile_definitions.html

[target_compile_options]: https://cmake.org/cmake/help/latest/command/target_compile_options.html

[target_include_directories]: https://cmake.org/cmake/help/latest/command/target_include_directories.html

[target_link_libraries]: https://cmake.org/cmake/help/latest/command/target_link_libraries.html

[target_link_options]: https://cmake.org/cmake/help/latest/command/target_link_options.html

[vcpkg]: https://github.com/Microsoft/vcpkg

[vcvarsall]: https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line#vcvarsall-syntax

[venv]: https://docs.python.org/3/tutorial/venv.html

[win32]: https://cmake.org/cmake/help/latest/variable/WIN32.html
