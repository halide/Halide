#!/usr/bin/python3

# Halide tutorial lesson 15: Generators: running AOT code with the standalone runtime

# This lesson demonstrates how to load and call a precompiled, ahead-of-time
# (AOT) compiled Halide pipeline at runtime using the standalone "halide.runtime"
# module.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_15_runtime
# in a shell with the current directory at python_bindings/

# Lesson 10 showed how to AOT-compile a pipeline and call it through a generated
# Python extension. This lesson shows another way to run AOT code: loading a
# precompiled shared library dynamically and calling it, using only
# "halide.runtime". Unlike "import halide", halide.runtime does not depend on
# libHalide (the compiler) at all -- it is a small package whose whole job is to
# run precompiled kernels.

# This is useful for deployment: you can compile your pipelines on a build
# machine that has the full Halide toolchain, then ship just the compiled kernels
# and this tiny runtime, and run them on machines that have neither the compiler
# nor LLVM installed.

# There are two distinct phases below:
#   1. Build time (needs the Halide compiler): compile a pipeline to a shared
#      library. In a real project this happens in your build system.
#   2. Deploy time (needs only halide.runtime): load that shared library and
#      call it. This is the part you would actually ship.

import os
import platform
import shutil
import subprocess
import tempfile

import numpy as np

# The standalone halide-runtime distribution is all you need to *call*
# precompiled kernels. This tutorial also has the full halide distribution
# available for the build-time compilation step below.
import halide.runtime as hlr


def compile_pipeline(directory):
    # Build time: AOT-compile a simple pipeline to a loadable shared library.
    # Returns the path to the shared library, or None if this environment cannot
    # build one (e.g. there is no C compiler on the PATH).
    import halide as hl

    # A simple one-stage pipeline that brightens an image, just like lesson 10.
    x, y = hl.Var("x"), hl.Var("y")
    input = hl.ImageParam(hl.UInt(8), 2, "input")
    offset = hl.Param(hl.Int(32), name="offset")
    brighter = hl.Func("brighter")
    brighter[x, y] = hl.cast(hl.UInt(8), input[x, y] + offset)
    brighter.vectorize(x, 16).parallel(y)

    # Compile to a static library using the host target for the current machine
    # so we can run it right away. This will generate a static library which bundles
    # the necessary Halide runtime for the host. So the shared library we link
    # below will be self-contained won't require the compiler from libHalide.
    #
    # Note: You can use the `no_runtime` target flag if you wish to create a
    #       stand-alone static library for just the kernel, but you would need
    #       to link in a matching runtime before you'd be able to run it.
    archive = os.path.join(directory, "brighter.a")
    brighter.compile_to(
        {hl.OutputFileType.static_library: archive},
        [input, offset],
        "brighter",
        hl.get_host_target(),
    )

    # halide.runtime loads a *shared* library, so link the static library into
    # one, taking care to keep the filter's symbols visible. In a real project
    # your build system does this for you; see doc/Python.md for a CMake recipe.
    return link_shared_library(archive, os.path.join(directory, "brighter"), "brighter")


def link_shared_library(archive, stem, name):
    # Link the static library `archive` into a shared library that exports the
    # filter's `<name>_argv` / `<name>_metadata` entry points, so halide.runtime
    # can resolve them. Returns the shared library path, or None if this
    # environment has no suitable linker.
    system = platform.system()

    # In a real project, you'd want to use a build system to do the linkage for
    # you, but for the sake of this tutorial, we'll try and invoke the various
    # platform toolchain's directly so we have a running example.
    if system == "Windows":
        # Use the MSVC linker (link.exe) if the Visual Studio toolchain is on the
        # PATH (e.g. from a Developer Command Prompt). We wrap the static library
        # into a DLL, listing the entry points to export in a .def file. /NOENTRY
        # is fine here: the DLL shares the host process's already-initialized
        # dynamic CRT, so it needs no startup code of its own.
        if shutil.which("link") is None:
            return None
        library = stem + ".dll"
        def_path = stem + ".def"
        with open(def_path, "w") as f:
            f.write("EXPORTS\n")
            f.write(f"    {name}_argv\n")
            f.write(f"    {name}_metadata\n")
        args = [
            "link",
            "/nologo",
            "/DLL",
            "/NOENTRY",
            archive,
            f"/DEF:{def_path}",
            f"/OUT:{library}",
        ]
    elif system == "Darwin":
        library = stem + ".dylib"
        cc = os.environ.get("CC", "cc")
        args = [cc, "-shared", "-o", library, "-Wl,-force_load," + archive]
    else:
        library = stem + ".so"
        cc = os.environ.get("CC", "cc")
        args = [
            cc,
            "-shared",
            "-o",
            library,
            "-Wl,--whole-archive",
            archive,
            "-Wl,--no-whole-archive",
            "-lpthread",
            "-ldl",
        ]

    try:
        subprocess.run(args, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return library


def main():
    with tempfile.TemporaryDirectory() as directory:
        library = compile_pipeline(directory)
        if library is None:
            # We couldn't produce a shared library to load in this environment,
            # and the interesting part of the lesson needs one.
            print("[SKIP] could not build a shared library to load")
            return 0

        # ------------------------------------------------------------------
        # Deploy time: everything below uses only halide.runtime, not
        # libHalide. This is the code you would actually ship to run
        # precompiled kernels.
        # ------------------------------------------------------------------

        # Load the precompiled kernel. The filter name defaults to the library's
        # file name; we pass it explicitly here for clarity.
        kernel = hlr.load(library, name="brighter")

        # A loaded Kernel knows what it was compiled for and how it must be
        # called. `kernel.arguments` describes the calling convention: one entry
        # per argument, with its name, kind (input_scalar / input_buffer /
        # output_buffer), element type, and dimensions.
        print("Loaded kernel:", kernel.name)
        print("Compiled for target:", kernel.target)
        for arg in kernel.arguments:
            print("  argument:", arg)

        # Let's make some input data to test with. Note that, as in lesson 10,
        # when a numpy array is passed to Halide code its axes are reversed;
        # since this pipeline is elementwise that doesn't affect the result.
        input = np.empty((640, 480), dtype=np.uint8)
        for y in range(480):
            for x in range(640):
                input[x, y] = (x ^ (y + 1)) & 0xFF

        # Halide does not allocate outputs for us, so we provide a buffer for the
        # result.
        output = np.empty((640, 480), dtype=np.uint8)

        offset = 5

        # Call it. Arguments may be passed by position, in the order given by
        # kernel.argument_names ...
        kernel(input, offset, output)
        # ... or by name, in the Python manner:
        # kernel(input=input, offset=offset, output=output)
        #
        # (Errors raise a Python exception rather than returning an int.)

        # Now let's check the filter performed as advertised: it was supposed to
        # add the offset to every input pixel, with uint8 wraparound.
        expected = (input.astype(np.int32) + offset).astype(np.uint8)
        assert np.array_equal(output, expected), "kernel produced the wrong result"

        # Everything worked!
        print("Success!")
        return 0


if __name__ == "__main__":
    main()
