# CMake Build Rules for Halide

## Overview

There are two main ways to use Halide in your application: as a **JIT compiler**
for dynamic pipelines or an **ahead-of-time (AOT) compiler** for static
pipelines.

CMake provides robust support for both use cases. These new features rely on
recent developments in CMake, so a recent version is necessary. In order to
build Halide, you need CMake version 3.16 or newer.

Previous releases bundled a `halide.cmake` module that was meant to be
`include()`-ed into your project. This has been removed. Now, you should load
Halide with the standard
[`find_package`](https://cmake.org/cmake/help/latest/command/find_package.html)
mechanism, like so:

```
cmake_minimum_required(VERSION 3.16)
project(HalideExample)

set(CMAKE_CXX_STANDARD 11)  # or newer
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)
```

You will need to add the base of the Halide installation directory to
`CMAKE_MODULE_PATH` at the CMake command line.

```
$ mkdir build
$ cd build
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MODULE_PATH="/path/to/Halide" ..
```

_**Note:**_ If you are using [`vcpkg`](https://github.com/Microsoft/vcpkg), this
is configured for you by the toolchain and you do not need to do anything
special to use Halide.

### Getting CMake

Getting a recent version of CMake couldn't be easier, and there are multiple
good options on any system to do so.

#### Cross-Platform

The Python package manager `pip` packages the newest version of CMake at all
times. If you already have a Python 3 installation, this should be as easy as
running

```
$ pip install --upgrade cmake
```

See the [PyPI website](https://pypi.org/project/cmake) for more details.

#### Windows

On Windows, you have two good options for installing CMake. First, if you have
**Visual Studio 2019** installed, you can get CMake 3.16 through the Visual
Studio installer. This is the recommended way of getting CMake if you are able
to use Visual Studio 2019. See the
[Visual C++ documentation](https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=vs-2019)
for more details.

**Otherwise**, you should not install CMake through Visual Studio, but rather
from [Kitware's website](https://cmake.org/download/).

#### macOS

On macOS, a
[recent version of CMake](https://formulae.brew.sh/cask/cmake#default) is
readily available from [Homebrew](https://brew.sh). Simply running

```
$ brew update
$ brew install cmake
```

should install the newest version of CMake.

The [releases on Kitware's website](https://cmake.org/download/) are also viable
options.

#### Linux

If you're on **Ubuntu Linux 20.04** (focal), then simply running
`sudo apt install cmake` is sufficient.

For older versions of **Debian, Ubuntu, Mint, and derivatives**, Kitware
provides an [APT repository](https://apt.kitware.com/) with up-to-date releases.
Note that this is still useful for Ubuntu 20.04 because it will remain up to
date.

For other Linux distributions, check with your distribution's package manager or
use pip as detailed above. Snap packages are also available.

This applies to WSL (Windows Subsystem for Linux) installations, too.

## Using Halide in JIT mode

To use Halide in JIT mode (like the
[tutorials](https://halide-lang.org/tutorials/tutorial_introduction.html) do,
for example), you can simply link to `Halide::Halide`.

```
# ... same project setup as before ...
add_executable(my_halide_app main.cpp)
target_link_libraries(my_halide_app PRIVATE Halide::Halide)
```

Then `Halide.h` will be available to your code and everything should just work.
That's it!

## Using Halide in AOT mode

Using Halide in AOT mode is more complicated so we'll walk through it step by
step. Note that this only applies to Halide generators, so it might be useful to
re-read the
[tutorial](https://halide-lang.org/tutorials/tutorial_lesson_15_generators.html)
on generators. Assume (like in the tutorial) that you have a source file named
`my_generators.cpp` and that you have generator classes `MyFirstGenerator` and
`MySecondGenerator` with registered names `my_first_generator` and
`my_second_generator` respectively.

Then the first step is to add a **Generator Executable** to your build:

```
# ... same project setup as before ...
add_executable(my_generators my_generators.cpp)
target_link_libraries(my_generators PRIVATE Halide::Generator)
```

Using the generator executable, we can add a Halide library corresponding to
`MyFirstGenerator`.

```
# ... continuing from above
add_halide_library(my_first_generator FROM my_generators)
```

This will create a
[STATIC IMPORTED](https://cmake.org/cmake/help/latest/command/add_library.html#imported-libraries)
library target in CMake that corresponds to the output of running your
generator. The second generator in the file requires generator parameters to be
passed to it. These are also easy to handle:

```
# ... continuing from above
add_halide_library(my_second_generator FROM my_generators
                   PARAMS parallel=false scale=3.0 rotation=ccw output.type=uint16)
```

Adding multiple configurations is easy, too:

```
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
target, which is also an IMPORTED library containing the Halide runtime. It is
transitively linked through `<GEN>` to targets that link to `<GEN>`. On an
operating system like Linux, where weak linking is available, this is not an
issue. However, on Windows, this will fail due to symbol redefinitions. In these
cases, you must declare that two Halide libraries share a runtime, like so:

```
# ... updating above
add_halide_library(my_second_generator_2 FROM my_generators
                   GENERATOR my_second_generator
                   USE_RUNTIME my_first_generator
                   PARAMS scale=9.0 rotation=ccw output.type=float32)

add_halide_library(my_second_generator_3 FROM my_generators
                   GENERATOR my_second_generator
                   USE_RUNTIME my_first_generator
                   PARAMS parallel=false output.type=float64)
```

This will even work correctly when different combinations of targets are
specified for each halide library. A "greatest common denominator" target will
be chosen that is compatible with all of them.

### Common pitfalls

Because it is not possible to demote targets from global scope, the targets
created by `add_halide_library` are not made global (like non-imported libraries
are) by default. If you want to access `my_first_generator` from a higher or
sibling directory, you must add the following line to your build:

```
set_target_properties(my_first_generator PROPERTIES IMPORTED_GLOBAL TRUE)
```

Be sure to look at the apps for more examples of how AOT mode in Halide is meant
to be used.

## Imported Targets

Halide defines the following targets that are available to users:

- `Halide::Halide` -- this is the JIT-mode library to use when using Halide from
  C++.
- `Halide::Generator` -- this is the target to use when defining a generator
  executable. It supplies a `main()` function.
- `Halide::Python` -- this is a Python 3 module that can be referenced as
  `$<TARGET_FILE:Halide::Python>` when setting up Python tests or the like from
  CMake.
- `Halide::Runtime` -- adds include paths to the Halide runtime headers
- `Halide::Tools` -- adds include paths to the Halide tools, including the
  benchmarking utility.
- `Halide::ImageIO` -- adds include paths to the Halide image io utility and
  sets up dependencies to PNG / JPEG if they are available.
- `Halide::RunGenMain` -- used with the `REGISTRATION` parameter of
  `add_halide_library` to create simple runners and benchmarking tools for
  Halide libraries.
- `Halide::Adams19` -- the Adams et.al. 2019 autoscheduler (no GPU support)
- `Halide::Li18` -- the Li et.al. 2018 gradient autoscheduler (limited GPU
  support)

## Functions

Currently, only one function is defined:

### `add_halide_library`

This is the main function for managing generators in AOT compilation. The full
signature follows:

```
add_halide_library(<target> FROM <generator-target>
                   [GENERATOR generator-name]
                   [FUNCTION_NAME function-name]
                   [USE_RUNTIME hl-target]
                   [PARAMS param1 [param2 ...]]
                   [TARGETS target1 [target2 ...]]
                   [FEATURES feature1 [feature2 ...]]
                   [PLUGINS plugin1 [plugin2 ...]]
                   [AUTOSCHEDULER scheduler-name]
                   [GRADIENT_DESCENT]
                   [C_BACKEND]
                   [REGISTRATION OUTVAR]
                   [<extra-output> OUTVAR])

extra-output = ASSEMBLY | BITCODE | COMPILER_LOG | CPP_STUB
             | FEATURIZATION | LLVM_ASSEMBLY | PYTHON_EXTENSION
             | PYTORCH_WRAPPER | SCHEDULE | STMT | STMT_HTML
```

This function creates a STATIC IMPORTED target called `<target>` corresponding
to running the `<generator-target>` (an executable target which links to
`Halide::Generator`) one time, using command line arguments derived from the
other parameters.

The arguments `GENERATOR` and `FUNCTION_NAME` default to `<target>`. They
correspond to the `-g` and `-f` command line flags, respectively.

If `USE_RUNTIME` is not specified, this function will create another target
called `<target>.runtime` which corresponds to running the generator with `-r`
and a compatible list of targets. This runtime target is an INTERFACE dependency
of `<target>`. If multiple runtime targets need to be linked together, setting
`USE_RUNTIME` to another Halide library, `<target2>` will prevent the generation
of `<target>.runtime` and instead use `<target2>.runtime`.

Parameters can be passed to a generator via the `PARAMS` argument. Parameters
should be space-separated. Similarly, `TARGETS` is a space-separated list of
targets for which to generate code in a single function. They must all share the
same platform/bits/os triple (eg. `arm-32-linux`). Features that are in common
among all targets, including device libraries (like `cuda`) should go in
`FEATURES`.

To set the default autoscheduler, set the `AUTOSCHEDULER` argument to a target
named like `Namespace::Scheduler`, for example `Halide::Adams19`. This will set
the `-s` flag on the generator command line to `Scheduler` and add the target to
the list of plugins. Additional plugins can be loaded by setting the `PLUGINS`
argument. If the argument to `AUTOSCHEDULER` does not contain `::` or it does
not name a target, it will be passed to the `-s` flag verbatim.

If `GRADIENT_DESCENT` is set, then the module will be built suitably for
gradient descent calculation in TensorFlow or PyTorch. See
`Generator::build_gradient_module()` for more documentation. This corresponds to
passing `-d 1` at the generator command line.

If the `C_BACKEND` option is set, this command will produce a `STATIC` library
target (not `IMPORTED`). It does this by supplying `c_source` to the `-e` flag
in place of `static_library` and then calling the currently configured C++
compiler. Note that a `<target>.runtime` target is _not_ created in this case,
and the `USE_RUNTIME` option is ignored. Other options work as expected.

If `REGISTRATION` is set, the path to the generated `.registration.cpp` file
will be set in `OUTVAR`. This can be used to generate a runner for a Halide
library that is useful for benchmarking and testing. For example:

```
add_halide_library(my_lib FROM my_gen REGISTRATION REG_CPP)
add_executable(my_gen_runner ${REG_CPP})
target_link_libraries(my_gen_runner PRIVATE my_lib Halide::RunGenMain)
```

This is equivalent to setting `-e registration`.

Lastly, each of the `extra-output` arguments directly correspond to an extra
output (via `-e`) from the generator. The value `OUTVAR` names a variable into
which a path (relative to `CMAKE_CURRENT_BINARY_DIR`) to the extra file will be
written.
