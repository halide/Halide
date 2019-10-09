#!/usr/bin/python3
# Halide tutorial lesson 3

# This lesson demonstrates how to inspect what the Halide compiler is producing.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_03_debugging_1
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_03*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_03 -std=c++11
# LD_LIBRARY_PATH=../bin ./lesson_03

# On os x:
# g++ lesson_03*.cpp -g -I ../include -L ../bin -lHalide -o lesson_03 -std=c++11
# DYLD_LIBRARY_PATH=../bin ./lesson_03

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

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
    output = gradient.realize(8, 8)

    # That line compiled and ran the pipeline. Try running this
    # lesson with the environment variable HL_DEBUG_CODEGEN set to
    # 1. It will print out the various stages of compilation, and a
    # pseudocode representation of the final pipeline.

    # If you set HL_DEBUG_CODEGEN to a higher number, you can see
    # more and more details of how Halide compiles your pipeline.
    # Setting HL_DEBUG_CODEGEN=2 shows the Halide code at each stage
    # of compilation, and also the llvm bitcode we generate at the
    # end.

    # If you'd prefer to read C code, the compile_to_c method emits C
    # code that implements the Halide pipeline. It can't compile
    # as-is without you also implementing some support functions, but
    # it can be helpful for understanding what the Halide pipeline is
    # doing. You pass it the name of the file, a list of arguments
    # the generated function should take (none in this case), and the
    # name of the generated function. Have a look inside gradient.cpp
    # after compiling and running this lesson.
    gradient.compile_to_c("gradient.cpp", [], "gradient")

    # Using these two tricks -- setting HL_DEBUG_CODEGEN and calling
    # compile_to_c -- you can usually figure out what code Halide is
    # generating. In the next lesson we'll see how to snoop on Halide
    # at runtime.

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
