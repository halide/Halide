#!/usr/bin/python3

# Halide tutorial lesson 3

# This lesson demonstrates how to inspect what the Halide compiler is
# producing.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_03_debugging_1
# in a shell with the current directory at python_bindings/

import halide as hl


def main():

    # We'll start by defining the simple single-stage imaging
    # pipeline from lesson 1.

    # This lesson will be about debugging, but unfortunately in C++,
    # objects don't know their own names, which makes it hard for us
    # to understand the generated code. To get around this, you can
    # pass a string to the hl.Func and hl.Var constructors to give them a
    # name for debugging purposes.
    gradient = hl.Func("gradient")
    x, y = hl.Var("x"), hl.Var("y")
    gradient[x, y] = x + y

    # Realize the function to produce an output image. We'll keep it
    # very small for this lesson.
    output = gradient.realize([8, 8])

    # That line compiled and ran the pipeline. Try running this
    # lesson with the environment variable HL_DEBUG_CODEGEN set to
    # 1. It will print out the various stages of compilation, and a
    # pseudocode representation of the final pipeline.

    # If you set HL_DEBUG_CODEGEN to a higher number, you can see
    # more and more details of how Halide compiles your pipeline.
    # Setting HL_DEBUG_CODEGEN=2 shows the Halide code at each stage
    # of compilation, and also the llvm bitcode we generate at the
    # end.

    # Halide will also output an HTML version of this output, which
    # supports syntax highlighting and code-folding, so it can be
    # nicer to read for large pipelines. Open gradient.stmt.html" with your
    # browser after running this tutorial.
    gradient.compile_to_lowered_stmt("gradient.stmt.html", [], hl.StmtOutputFormat.HTML)

    # You can usually figure out what code Halide is generating using
    # this pseudocode. In the next lesson we'll see how to snoop on
    # Halide at runtime.

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
