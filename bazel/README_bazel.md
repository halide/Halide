# Bazel Build Rules for Halide

## Release Notes

These build rules are known to work well for Linux and OSX systems. Windows 
support is still a work-in-progress, and may require tweaking for now. These
rules are intended to replace the experimental rules implementation provided 
by https://github.com/halide/halide_bazel, which are now considered
deprecated.

Bazel 0.6 or later is required.

## Overview

The primary support for Halide in Bazel is the `halide_library` rule, which
allows you to build a Generator into an executable for various architectures.

Halide code is produced in a two-stage build that may seem a bit unusual at
first, so before documenting the build rules, let's recap: a *Generator* is a 
C++ class in which you define a Halide pipeline and schedule, as well as 
well-defined input and output parameters; it can be compiled into an executable 
(often referred to as a *Filter*) that efficiently runs the Halide pipeline.

## Halide Compilation Process

The Halide compilation process is actually several steps:

1.  Your Generator is compiled using a standard C++ compiler.
2.  It's linked with libHalide, LLVM, and a simple command-line wrapper.
    This results in an executable known as the *Generator Executable*.
3.  The Generator Executable is run *as a tool, at Bazel build time*; this
    produces a .a, .h, and a few other Halide-specific files.
4.  The resulting .a and .h are bundled into a cc_library() rule.

The `halide_library` rule (mostly) hides the details of this, with some
unavoidable warts, the main one being that there are two distinct `deps`
possibilities:

1.  Generator deps: Bazel deps that are necessary to compile and/or link the
    Generator itself.
2.  Filter deps: Bazel deps that are necessary to link and/or run the Filter.
    (Generally speaking, you only need Filter deps if you use Halide's 
    `define_extern` directive.)

## Using the Halide Build Rules

The easiest way to use Bazel rules for Halide is via a prebuilt Halide 
distribution that includes them. In your Bazel WORKSPACE file, just add an
`http_archive` directive that points to the release you want, e.g.,

    http_archive(
      name = "halide",
      urls = ["https://github.com/halide/Halide/releases/download/release_2049_01_01/halide-linux-64-gcc48-trunk-0123456789abcdef0123456789abcdef01234567.tgz"],
      sha256 = "sha256-of-the-release-goes-here"
      strip_prefix = "halide",
    )

If you want to work with a local copy of Halide, you can just use a `local_repository`
rule instead:

    # This assumes you have built 'make distrib' locally, of course
    local_repository(
      name = "halide",
      path = "/path/to/Halide/distrib",
    )

Then, in your own project's BUILD files, just add

    load("@halide//:halide.bzl", "halide_library")

and you're good to go.

## Build Rules

```
halide_library(name, srcs, copts, debug_codegen_level, extra_outputs
   filter_deps, function_name, generator_args, generator_deps, generator_name, 
   halide_target_features, halide_target_map, includes, namespace, visibility)
```

*   **name** *(Name; required)* The name of the build rule.
*   **srcs** *(List of labels; required)* source file(s) to compile into the
    Generator Executable.
*   **copts** *(List of strings; optional)* Options to be passed to the C++
    compiler when building the Generator.
*   **debug_codegen_level** *(Integer; optional)* Value to use for
    HL_DEBUG_CODEGEN when building the Generator; usually useful only for
    advanced Halide debugging. Defaults to zero. 
*   **extra_outputs** *(List of strings; optional)* A list of extra Halide
    outputs to generate at build time; this is exclusively intended for
    debugging (e.g. to examine Halide code generation) and currently supports:

    *   "assembly" (generate assembly listings for the generated functions)
    *   "bitcode" (emit the LLVM bitcode for generation functions)
    *   "stmt" (generate Halide .stmt files for generated functions)
    *   "html" (like "stmt", but generated with HTML-formatted wrapping)

*   **filter_deps** *(List of labels; optional)* optional list of dependencies
    needed by the Filter.

*   **function_name** *(String; optional)* The name of the generated C function
    for the filter. If omitted, defaults to the Bazel rule name.

*   **generator_args** *(String; optional)* Arguments to pass to the Generator,
    used to define the compile-time values of GeneratorParams defined by the
    Generator. If any undefined GeneratorParam names (or illegal GeneratorParam
    values) are specified, a compile error will result.

*   **generator_deps** *(List of labels; optional)* optional list of
    dependencies needed by the Generator. (These dependencies are *not* included
    in the filter produced by `halide_library`, nor in any executable that
    depends on it.)

*   **generator_name** *(String; optional)* The registered name of the Halide
    generator (i.e., the name passed as the second argument to HALIDE_REGISTER_GENERATOR
    in the Generator source file). If empty (or omitted), assumed to be the same
    as the Bazel rule name.

*   **halide_target_features** *(List of strings; optional)* A list of extra
    Halide Features to enable in the code. This can be any feature listed in
    `feature_name_map` in `src/Target.cpp`.
    The most useful are generally:

    *   "debug" (generate code with extra debugging)
    *   "cuda", "opengl", or "opencl" (generate code for a GPU target)
    *   "profile" (generate code with Halide's sampling profiler included)
    *   "user_context" (the generated Filter function to take an arbitrary void*
        pointer as the first parameter)

*   **halide_target_map** *(Dict of List-of-strings; optional)* This allows you
    to selectively (or completely) override the default Halide Targets chosen
    for each basic architecture by the rule. Each key is a Target with only
    arch-os-bits; each value is an ordered list of Targets with (possibly)
    additional interesting features, in the order desired. For example, the
    default value for this argument might contain:

        halide_target_map = {
            "x86-64-linux": [
                "x86-64-linux-sse41-avx-avx2",
                "x86-64-linux-sse41-avx",
                "x86-64-linux-sse41",
                "x86-64-linux",
            ],
            "x86-32-linux": [ ... ]
            "arm-32-android": [ ... ]
            # etc
        }

    This means "When the build target is any variant of x86-64 for Linux,
    generate specializations for AVX2, AVX, SSE41, and none-of-the-above; at
    runtime, check *in that order* until finding one that can be safely executed
    on the current processor."

    We can use this facility to add extra specializations, e.g.:

        halide_target_map = {
            "x86-64-linux": [
                # Specialize for f16c and try it before anything else
                "x86-64-linux-sse41-avx-avx2-f16c",
                "x86-64-linux-sse41-avx-avx2",
                "x86-64-linux-sse41-avx",
                "x86-64-linux-sse41",
                "x86-64-linux",
            ],
        }

    Alternately, we can use it to specialize output when we know the target
    hardware specifically, e.g.:

        halide_target_map = {
            "x86-32-android": [
                # This filter is only intended for a specific Android device,
                # which we happen to know supports SSE4.1 but nothing more.
                "x86-32-android-sse41"
            ],
        }

    Note that any features specified in the `halide_target_features` will be
    added to *every* target in `halide_target_map`, so specifying

        halide_target_features = [ "debug" ],
        halide_target_map = {
            "x86-64-linux": [
                "x86-64-linux-sse41"
            ],
        }

    would actually produce code using an effective target of
    `x86-64-linux-sse41-debug`.

*   **includes** *(List of strings; optional)* The list of paths to search
    include files when building the Generator.

*   **namespace** *(String; optional)* Namespace of the generated function. If
    specified, will create a C++ function that forwards to a C function that is
    generated by prefixing it with the namespace name replacing "::" with "_".

*   **visibility** *(List of labels; optional)* Bazel visibility of result.

`halide_library` returns the fully-qualified Bazel build target, so it can be
used in list comprehensions in BUILD files, e.g.:

    # filters will be ["//my/pkg:filter0", "//my/pkg:filter1", ...]
    filters = [halide_library(name="filter%d" % variant, ...) for variant in [0, 1, 2, 3]]

## Build Rules (Advanced)

Most code can use the `halide_library` rule directly, but for some usage, it can
be desirable to directly use two lower-level rules.

```
halide_generator(name, srcs, copts, deps, generator_name, includes, tags, visibility)
```

The `halide_generator` rule compiles Generator source code into an executable;
it corresponds to steps 1 and 2 in the [Halide Compilation
Process](#halide-compilation-process).

*   **name** *(Name; required)* The name of the build rule.

*   **srcs** *(List of labels; required)* source file(s) to compile into the
    Generator Executable.

*   **copts** *(List of strings; optional)* Options to be passed to the C++
    compiler when building the Generator.

*   **deps** *(List of labels; optional)* optional list of dependencies needed
    by the Generator.

*   **generator_name** *(String; optional)* The registered name of the Halide
    generator (i.e., the name passed as the second argument to HALIDE_REGISTER_GENERATOR
    in the Generator source file). If empty (or omitted), assumed to be the same
    as the Bazel rule name.

*   **includes** *(List of strings; optional)* The list of paths to search
    include files when building the Generator.

*   **tags** *(List of strings; optional)* List of arbitrary text tags. Tags may
    be any valid string; default is the empty list.

*   **visibility** *(List of labels; optional)* Bazel visibility of result.

```
halide_library_from_generator(name, generator, debug_codegen_level,
    deps, extra_outputs, function_name, generator_args, halide_target_features, 
    halide_target_map, namespace, tags, visibility)
```

The `halide_library_from_generator` rule executes a compiled Generator to
produce final object code and related files; it corresponds to steps 3 and 4 in
the [Halide Compilation Process](#halide-compilation-process).

*   **name** *(Name; required)* The name of the build rule.

*   **generator** *(Label; required)* The Bazel label for a `halide_generator`
    rule.

*   **debug_codegen_level** *(Integer; optional)* Value t∏ use for
    HL_DEBUG_CODEGEN when building the Generator; usually useful only for
    advanced Halide debugging. Defaults to zero.

*   **deps** *(List of labels; optional)* optional list of dependencies needed
    by the Filter.

*   **extra_outputs** *(List of strings; optional)* A list of extra Halide
    outputs to generate at build time.

*   **function_name** *(String; optional)* The name of the generated C function
    for the filter. If omitted, defaults to the Bazel rule name.

*   **generator_args** *(String; optional)* Arguments to pass to the Generator,
    used to define the compile-time values of GeneratorParams defined by the
    Generator. If any undefined GeneratorParam names (or illegal GeneratorParam
    values) are specified, a compile error will result.

*   **halide_target_features** *(List of strings; optional)* A list of extra
    Halide Features to enable in the code. This can be any feature listed in
    `feature_name_map` in `src/Target.cpp`.

*   **halide_target_map** *(Dict of List-of-strings; optional)* This allows you
    to selectively (or completely) override the default Halide Targets chosen
    for each basic architecture by the rule. Each key is a Target with only
    arch-os-bits; each value is an ordered list of Targets with (possibly)
    additional interesting features, in the order desired.

*   **namespace** *(String; optional)* Namespace of the generated function. If
    specified, will create a C++ function that forwards to a C function that is
    generated by prefixing it with the namespace name replacing "::" with "_".

*   **tags** *(List of strings; optional)* List of arbitrary text tags. Tags may
    be any valid string; default is the empty list.

*   **visibility** *(List of labels; optional)* Bazel visibility of result.

`halide_library_from_generator` returns the fully-qualified Bazel build target,
so it can be used in list comprehensions in BUILD files.

## Multi-Architecture Support

In some cases, it's desirable to have Halide generate multiple variants of the
same Filter, and select the most apppropriate one at runtime; the most common of
these is for x86-64, where you'd like to take advantage of AVX/AVX2 when
possible, falling back to SSE4.1 or SSE2 if necessary. Most x86 targets
have reasonable defaults, but this behavior can be customized as described
under the `halide_target_map` sections, above.
