// Halide tutorial lesson 1: Getting started with Funcs, Vars, and Exprs

// This lesson demonstrates basic usage of Halide as a JIT compiler for imaging.

// On linux, you can compile and run it like so:
// g++ lesson_01*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_01 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_01

// On os x:
// g++ lesson_01*.cpp -g -I ../include -L ../bin -lHalide -o lesson_01 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_01

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_01_basics
// in a shell with the current directory at the top of the halide
// source tree.

// The only Halide header file you need is Halide.h. It includes all of Halide.
#include "Halide.h"

// We'll also include stdio for printf.
#include <stdio.h>

int main(int argc, char **argv) {

    // This program defines a single-stage imaging pipeline that
    // outputs a grayscale diagonal gradient.

    // A 'Func' object represents a pipeline stage. It's a pure
    // function that defines what value each pixel should have. You
    // can think of it as a computed image.
    Halide::Func gradient;

    // Var objects are names to use as variables in the definition of
    // a Func. They have no meaning by themselves.
    Halide::Var x, y;

    // We typically use Vars named 'x' and 'y' to correspond to the x
    // and y axes of an image, and we write them in that order. If
    // you're used to thinking of images as having rows and columns,
    // then x is the column index, and y is the row index.

    // Funcs are defined at any integer coordinate of its variables as
    // an Expr in terms of those variables and other functions.
    // Here, we'll define an Expr which has the value x + y. Vars have
    // appropriate operator overloading so that expressions like
    // 'x + y' become 'Expr' objects.
    Halide::Expr e = x + y;

    // Now we'll add a definition for the Func object. At pixel x, y,
    // the image will have the value of the Expr e. On the left hand
    // side we have the Func we're defining and some Vars. On the right
    // hand side we have some Expr object that uses those same Vars.
    gradient(x, y) = e;

    // This is the same as writing:
    //
    //   gradient(x, y) = x + y;
    //
    // which is the more common form, but we are showing the
    // intermediate Expr here for completeness.

    // That line of code defined the Func, but it didn't actually
    // compute the output image yet. At this stage it's just Funcs,
    // Exprs, and Vars in memory, representing the structure of our
    // imaging pipeline. We're meta-programming. This C++ program is
    // constructing a Halide program in memory. Actually computing
    // pixel data comes next.

    // Now we 'realize' the Func, which JIT compiles some code that
    // implements the pipeline we've defined, and then runs it.  We
    // also need to tell Halide the domain over which to evaluate the
    // Func, which determines the range of x and y above, and the
    // resolution of the output image. Halide.h also provides a basic
    // templatized image type we can use. We'll make an 800 x 600
    // image.
    Halide::Buffer<int32_t> output = gradient.realize(800, 600);

    // Halide does type inference for you. Var objects represent
    // 32-bit integers, so the Expr object 'x + y' also represents a
    // 32-bit integer, and so 'gradient' defines a 32-bit image, and
    // so we got a 32-bit signed integer image out when we call
    // 'realize'. Halide types and type-casting rules are equivalent
    // to C.

    // Let's check everything worked, and we got the output we were
    // expecting:
    for (int j = 0; j < output.height(); j++) {
        for (int i = 0; i < output.width(); i++) {
            // We can access a pixel of an Buffer object using similar
            // syntax to defining and using functions.
            if (output(i, j) != i + j) {
                printf("Something went wrong!\n"
                       "Pixel %d, %d was supposed to be %d, but instead it's %d\n",
                       i, j, i+j, output(i, j));
                return -1;
            }
        }
    }

    // Everything worked! We defined a Func, then called 'realize' on
    // it to generate and run machine code that produced an Buffer.
    printf("Success!\n");

    return 0;
}
