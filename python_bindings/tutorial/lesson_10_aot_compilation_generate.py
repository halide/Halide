#!/usr/bin/python3

# Halide tutorial lesson 10.

# This lesson demonstrates how to use Halide as an more traditional
# ahead-of-time (AOT) compiler.

# This lesson is split across two files. The first (this one), builds
# a Halide pipeline and compiles it to an object file, a header and
# a Python extension. The second (lesson_10_aot_compilation_run.py),
# uses that object file to actually run the pipeline. This means that
# compiling this code is a multi-step process.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_10_aot_compilation_generate
# in a shell with the current directory at python_bindings/

# This will generate a file lesson_10_halide.py.cpp that still needs
# to be compiled. Use
#    make make test_tutorial_lesson_10_aot_compilation_run
# to generate and run a Python module called "lesson_10_halide".

# The benefits of this approach are that the final program:
# - Doesn't do any jit compilation at runtime, so it's fast.
# - Doesn't depend on libHalide at all, so it's a small, easy-to-deploy binary.

import halide as hl


def main():

    # We'll define a simple one-stage pipeline:
    brighter = hl.Func("brighter")
    x, y = hl.Var("x"), hl.Var("y")

    # The pipeline will depend on one scalar parameter.
    offset = hl.Param(hl.UInt(8), name="offset")

    # And take one grayscale 8-bit input buffer. The first
    # constructor argument gives the type of a pixel, and the second
    # specifies the number of dimensions (not the number of
    # channels!). For a grayscale image this is two for a color
    # image it's three. Currently, four dimensions is the maximum for
    # inputs and outputs.
    input = hl.ImageParam(hl.UInt(8), 2)

    # If we were jit-compiling, these would just be an int and a
    # hl.Buffer, but because we want to compile the pipeline once and
    # have it work for any value of the parameter, we need to make a
    # hl.Param object, which can be used like an hl.Expr, and an hl.ImageParam
    # object, which can be used like a hl.Buffer.

    # Define the hl.Func.
    brighter[x, y] = input[x, y] + offset

    # Schedule it.
    brighter.vectorize(x, 16).parallel(y)

    # This time, instead of calling brighter.realize(...), which
    # would compile and run the pipeline immediately, we'll call a
    # method that compiles the pipeline to an object file and header.
    #
    # For AOT-compiled code, we need to explicitly declare the
    # arguments to the routine. This routine takes two. Arguments are
    # usually Params or ImageParams.
    fname = "lesson_10_halide"
    brighter.compile_to({hl.OutputFileType.object: "lesson_10_halide.o",
                         hl.OutputFileType.python_extension: "lesson_10_halide.py.cpp"},
                        [input, offset], "lesson_10_halide")

    print("Halide pipeline compiled, but not yet run.")

    # To continue this lesson, look in the file
    # lesson_10_aot_compilation_run.py

    return 0


if __name__ == "__main__":
    main()
