// Halide tutorial lesson 1.

// This lesson demonstrates basic usage of Halide as a JIT compiler for imaging.

// On linux, you can compile and run it like so:
// g++ lesson_01.cpp -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_01
// LD_LIBRARY_PATH=../bin ./lesson_01 

// The only Halide header file you need is Halide.h
#include <Halide.h>

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
    // a Func.
    Halide::Var x, y;

    // Now we'll add a definition for the Func object. At pixel x, y,
    // the image will have the value x + y. This line of code defines
    // the Func, but doesn't actually compute the output image yet.
    // That will happen later.
    gradient(x, y) = x + y;

    // Halide does type inference for you. Vars are 32-bit integers,
    // so "x + y" is a also 32-bit integer, so 'gradient' defines a
    // 32-bit image.

    // Now we 'realize' the Func, which JIT compiles some code that
    // implements the pipeline we've defined, and runs it. We also
    // need to tell Halide the domain over which to evaluate the Func,
    // which determines the range of x and y above, and the resolution
    // of the output image. Halide.h also provides a basic templatized
    // Image type. We'll make an 800 x 600 image.
    Halide::Image<int32_t> output = gradient.realize(800, 600);
    
    // Let's check everything worked, and we got the output we were
    // expecting:
    for (int j = 0; j < output.height(); j++) {
        for (int i = 0; i < output.width(); i++) {
            // We can access a pixel of an Image object using similar
            // syntax to defining and using functions. 
            if (output(i, j) != i + j) {
                printf("Something went wrong!\n"
                       "Pixel %d, %d was supposed to be %d, but instead it's %d\n", 
                       i, j, i+j, output(i, j));
                return -1;
            }
        }
    }

    // Everything worked! 
    printf("Success!\n");

    return 0;
}
