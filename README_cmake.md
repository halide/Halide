# CMake Build Rules for Halide

## Overview

The primary support for Halide in CMake is the
[`halide_library`](#halide_library) rule, which allows you to build a Generator
into an executable for various architectures.

Halide code is produced in a two-stage build that may seem a bit unusual at
first, so before documenting the build rules, let's recap: a *Generator* is a
C++ class in which you define a Halide pipeline and schedule, as well as
well-defined input and output parameters; it can be compiled into an executable
(often referred to as a *Filter*) that efficiently runs the Halide pipeline.

## Halide Compilation Process

The Halide compilation process is actually several steps:

1.  Your Generator is compiled using a standard C++ compiler.
2.  It's linked with libHalide, LLVM, and a simple command-line wrapper. This
    results in an executable known as the *Generator Executable*.
3.  The Generator Executable is run *as a tool, at build time*; this produces a
    .a, .h, and a few other Halide-specific files.
4.  The resulting .a and .h are bundled into a CMake add_library() rule.

The [`halide_library`](#halide_library) rule (mostly) hides the details of this,
with some unavoidable warts, the main one being that there are two distinct sort
of dependencies involved:

1.  Generator dependencies: dependencies that are necessary to compile and/or
    link the Generator itself.
2.  Filter dependencies: dependencies that are necessary to link and/or run the
    Filter. (Generally speaking, you only need Filter dependencies if you use
    Halide's `define_extern` directive.)

## Using the Halide Build Rules

The easiest way to use CMake rules for Halide is via a prebuilt Halide
distribution that includes them. In your CMakeLists.txt file, just set
`HALIDE_DISTRIB_DIR` to point to the distribution directory, then include
`halide.cmake`:

    set(HALIDE_DISTRIB_DIR "/path/to/halide/distrib")
    include("${HALIDE_DISTRIB_DIR}/halide.cmake")

Then, using a Generator can be as simple as

    halide_library(coolness SRCS coolness_generator.cpp)

    add_executable(my_app my_app.cpp)
    target_link_libraries(my_app PUBLIC coolness)

For an example of "standalone" use of the CMake rules, see [apps/wavelet](apps/wavelet).

## Build Rules

### halide_library

```
halide_library(name, srcs, extra_outputs, filter_deps, function_name,
generator_args, generator_deps, generator_name, halide_target,
halide_target_features, includes)
```

*   **name** *(Name; required)* The name of the library target.

*   **srcs** *(List of C++ source files; required)* source file(s) to compile
    into the Generator Executable.

*   **extra_outputs** *(List of strings; optional)* A list of extra Halide
    outputs to generate at build time; this is exclusively intended for
    debugging (e.g. to examine Halide code generation) and currently supports:

    *   "assembly" (generate assembly listings for the generated functions)
    *   "bitcode" (emit the LLVM bitcode for generation functions)
    *   "stmt" (generate Halide .stmt files for generated functions)
    *   "html" (like "stmt", but generated with HTML-formatted wrapping)

*   **filter_deps** *(List of CMake targets; optional)* optional list of
    dependencies needed by the Filter.

*   **function_name** *(String; optional)* The name of the generated C function
    for the filter. If omitted, defaults to the CMake rule name.

*   **generator_args** *(String; optional)* Arguments to pass to the Generator,
    used to define the compile-time values of GeneratorParams defined by the
    Generator. If any undefined GeneratorParam names (or illegal GeneratorParam
    values) are specified, a compile error will result.

*   **generator_deps** *(List of CMake targets; optional)* optional list of
    dependencies needed by the Generator. (These dependencies are *not* included
    in the filter produced by [`halide_library`](#halide_library), nor in any
    executable that depends on it.)

*   **generator_name** *(String; optional)* The registered name of the Halide
    generator (i.e., the name passed as the second argument to
    HALIDE_REGISTER_GENERATOR in the Generator source file). If omitted,
    defaults to the CMake rule name.

*   **halide_target** *(String; optional)* The Halide target-string to use when
    generating code; if omitted, defaults to "host". If you want to target for a
    specific architechture, or to enable certain features, you can specify
    specific values, e.g. "x86-64-linux-sse41-avx" compiles for x86-64 on Linux,
    also enabling SSE4.1 and AVX. You can also specify multiple values,
    separated by commas, to generate several variants that are selected at
    runtime; e.g. "x86-64-linux-sse41-avx,x86-64-linux-sse41,x86-64-linux" will
    generate three versions of the code (for SSE4.1+AVX, for SSE4.1, and for
    plain x86-64), and select the first matching variants at runtime. (See also
    [*halide_target* vs
    *halide_target_features*](#halide_target-vs-halide_target_features) below.)

*   **halide_target_features** *(List of strings; optional)* A list of extra
    Halide Features to enable in the code. This can be any feature listed in
    `feature_name_map` in `src/Target.cpp`. This is typically where flags to
    enable GPU features should go. (See also [*halide_target* vs
    *halide_target_features*](#halide_target-vs-halide_target_features) below.)

*   **includes** *(List of strings; optional)* The list of paths to search
    include files when building the Generator.

### *halide_target* vs *halide_target_features*

What's the difference between these two? It's a little subtle, and their uses
overlap a bit. The Halide target string used to generate code found by appending
all the features in `halide_target_features` onto `halide_target`; this can be
useful in complex builds in which you target multiple architectures, with
additional features tacked on. For instance, say we want to generate libraries
for x86 and ARM architectures, with OpenGL supported on all of them:

    foreach(ARCH x86 arm)
        foreach(BITS 32 64)
            halide_library(spiffy_${ARCH}_${BITS}
                           SRCS spiffy_generator.cpp
                           HALIDE_TARGET ${ARCH}-${BITS}-android
                           HALIDE_TARGET_FEATURES opengl)
        endforeach()
    endforeach()

The distinction can also be useful if you are generating a multi-architecture
library:

    halide_library(nifty
                   SRCS nifty_generator.cpp
                   HALIDE_TARGET x86-64-osx-sse41-avx,x86-64-osx-sse41,x86-64-osx-sse41
                   HALIDE_TARGET_FEATURES metal)

This says "Generate the library with specializations for AVX+SSE4.1, SSE4.1, and
plain x86-64, and select the proper one at runtime... but enable the Metal
feature for all three."

## Build Rules (Advanced)

Most code can use the [`halide_library`](#halide_library) rule directly, but for
some usage, it can be desirable to directly use two lower-level rules.

### halide_generator

```
halide_generator(name, srcs, deps, generator_name, includes)
```

The [`halide_generator`](#halide_generator) rule compiles Generator source code
into an executable; it corresponds to steps 1 and 2 in the [Halide Compilation
Process](#halide-compilation-process).

*   **name** *(Name; required)* The name of the executable target. Note that
    target names for [`halide_generator`](#halide_generator) are required to end
    in `.generator`.

*   **srcs** *(List of C++ source files; required)* source file(s) to compile
    into the Generator Executable.

*   **deps** *(List of CMake targets; optional)* optional list of dependencies
    needed by the Generator.

*   **generator_name** *(String; optional)* The registered name of the Halide
    generator (i.e., the name passed as the second argument to
    HALIDE_REGISTER_GENERATOR in the Generator source file). If omitted,
    defaults to the CMake rule name.

*   **includes** *(List of paths; optional)* The list of paths to search include
    files when building the Generator.

### halide_library_from_generator

```
halide_library_from_generator(name, generator, deps, extra_outputs,
function_name, generator_args, halide_target, halide_target_features)
```

The [`halide_library_from_generator`](#halide_library_from_generator) rule
executes a compiled Generator to produce final object code and related files; it
corresponds to steps 3 and 4 in the [Halide Compilation
Process](#halide-compilation-process).

*   **name** *(Name; required)* The name of the library target.

*   **generator** *(CMake target; required)* The CMake target for a
    [`halide_generator`](#halide_generator) rule.

*   **deps** *(List of CMake targets; optional)* optional list of dependencies
    needed by the Filter.

*   **extra_outputs** *(List of strings; optional)* A list of extra Halide
    outputs to generate at build time; this is exclusively intended for
    debugging (e.g. to examine Halide code generation) and currently supports:

    *   "assembly" (generate assembly listings for the generated functions)
    *   "bitcode" (emit the LLVM bitcode for generation functions)
    *   "stmt" (generate Halide .stmt files for generated functions)
    *   "html" (like "stmt", but generated with HTML-formatted wrapping)

*   **function_name** *(String; optional)* The name of the generated C function
    for the filter. If omitted, defaults to the CMake rule name.

*   **generator_args** *(String; optional)* Arguments to pass to the Generator,
    used to define the compile-time values of GeneratorParams defined by the
    Generator. If any undefined GeneratorParam names (or illegal GeneratorParam
    values) are specified, a compile error will result.

*   **halide_target** *(String; optional)* The Halide target-string to use when
    generating code; if omitted, defaults to "host". See
    [`halide_library`](#halide_library) for more information.

*   **halide_target_features** *(List of strings; optional)* A list of extra
    Halide Features to enable in the code. See
    [`halide_library`](#halide_library) for more information.
