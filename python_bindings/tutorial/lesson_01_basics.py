#!/usr/bin/python3

# Halide tutorial lesson 1.

# This lesson demonstrates basic usage of Halide as a JIT compiler for imaging.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_01_basics
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_01*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_01 -std=c++11
# LD_LIBRARY_PATH=../bin ./lesson_01

# On os x:
# g++ lesson_01*.cpp -g -I ../include -L ../bin -lHalide -o lesson_01 -std=c++11
# DYLD_LIBRARY_PATH=../bin ./lesson_01

# The only Halide header file you need is Halide.h. It includes all of Halide.
#include "Halide.h"

# We'll also include stdio for printf.
#include <stdio.h>

import halide as hl

def main():

    # This program defines a single-stage imaging pipeline that
    # outputs a grayscale diagonal gradient.

    # A 'hl.Func' object represents a pipeline stage. It's a pure
    # function that defines what value each pixel should have. You
    # can think of it as a computed image.
    gradient = hl.Func("gradient")

    # hl.Var objects are names to use as variables in the definition of
    # a hl.Func. They have no meaning by themselves.
    x, y = hl.Var("x"), hl.Var("y")

    # We typically use Vars named 'x' and 'y' to correspond to the x
    # and y axes of an image, and we write them in that order. If
    # you're used to thinking of images as having rows and columns,
    # then x is the column index, and y is the row index.

    # Funcs are defined at any integer coordinate of its variables as
    # an hl.Expr in terms of those variables and other functions.
    # Here, we'll define an hl.Expr which has the value x + y. Vars have
    # appropriate operator overloading so that expressions like
    # 'x + y' become 'hl.Expr' objects.
    e = x + y
    assert type(e) == hl.Expr

    # Now we'll add a definition for the hl.Func object. At pixel x, y,
    # the image will have the value of the hl.Expr e. On the left hand
    # side we have the hl.Func we're defining and some Vars. On the right
    # hand side we have some hl.Expr object that uses those same Vars.
    gradient[x, y] = e

    # This is the same as writing:
    #
    #   gradient[x, y] = x + y
    #
    # which is the more common form, but we are showing the
    # intermediate hl.Expr here for completeness.

    # That line of code defined the hl.Func, but it didn't actually
    # compute the output image yet. At this stage it's just Funcs,
    # Exprs, and Vars in memory, representing the structure of our
    # imaging pipeline. We're meta-programming. This C++ program is
    # constructing a Halide program in memory. Actually computing
    # pixel data comes next.

    # Now we 'realize' the hl.Func, which JIT compiles some code that
    # implements the pipeline we've defined, and then runs it.  We
    # also need to tell Halide the domain over which to evaluate the
    # hl.Func, which determines the range of x and y above, and the
    # resolution of the output image. Halide.h also provides a basic
    # templatized image type we can use. We'll make an 800 x 600
    # image.
    output = gradient.realize(800, 600)
    assert output.type() == hl.Int(32)

    # Halide does type inference for you. hl.Var objects represent
    # 32-bit integers, so the hl.Expr object 'x + y' also represents a
    # 32-bit integer, and so 'gradient' defines a 32-bit image, and
    # so we got a 32-bit signed integer image out when we call
    # 'realize'. Halide types and type-casting rules are equivalent
    # to C.

    # Let's check everything worked, and we got the output we were
    # expecting:
    for j in range(output.height()):
        for i in range(output.width()):
            # We can access a pixel of an hl.Buffer object using similar
            # syntax to defining and using functions.
            if (output[i, j] != i + j):
                print("Something went wrong!\n"
                       "Pixel %d, %d was supposed to be %d, but instead it's %d\n"
                        % (i, j, i+j, output[i, j]))
                return -1


    # Everything worked! We defined a hl.Func, then called 'realize' on
    # it to generate and run machine code that produced a hl.Buffer.
    print("Success!")

    return 0

if __name__ == "__main__":
    main()
