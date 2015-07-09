// Halide tutorial lesson 3

// This lesson demonstrates how to inspect what the Halide compiler is producing.

// On linux, you can compile and run it like so:
// g++ lesson_03*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_03 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_03

// On os x:
// g++ lesson_03*.cpp -g -I ../include -L ../bin -lHalide -o lesson_03 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_03

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_03_debugging_1
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

// This time we'll just import the entire Halide namespace
using namespace Halide;

int main(int argc, char **argv) {

    // We'll start by defining the simple single-stage imaging
    // pipeline from lesson 1.

    // This lesson will be about debugging, but unfortunately in C++,
    // objects don't know their own names, which makes it hard for us
    // to understand the generated code. To get around this, you can
    // pass a string to the Func and Var constructors to give them a
    // name for debugging purposes.
    Func gradient("gradient");
    Var x("x"), y("y");
    gradient(x, y) = x + y;

    // Realize the function to produce an output image. We'll keep it
    // very small for this lesson.
    Image<int> output = gradient.realize(8, 8);

    // That line compiled and ran the pipeline. Try running this
    // lesson with the environment variable HL_DEBUG_CODEGEN set to
    // 1. It will print out the various stages of compilation, and a
    // pseudocode representation of the final pipeline.

    // If you set HL_DEBUG_CODEGEN to a higher number, you can see
    // more and more details of how Halide compiles your pipeline.
    // Setting HL_DEBUG_CODEGEN=2 shows the Halide code at each stage
    // of compilation, and also the llvm bitcode we generate at the
    // end.

    // If you'd prefer to read C code, the compile_to_c method emits C
    // code that implements the Halide pipeline. It can't compile
    // as-is without you also implementing some support functions, but
    // it can be helpful for understanding what the Halide pipeline is
    // doing. You pass it the name of the file, a list of arguments
    // the generated function should take (none in this case), and the
    // name of the generated function. Have a look inside gradient.cpp
    // after compiling and running this lesson.
    gradient.compile_to_c("gradient.cpp", std::vector<Argument>(), "gradient");

    // Using these two tricks -- setting HL_DEBUG_CODEGEN and calling
    // compile_to_c -- you can usually figure out what code Halide is
    // generating. In the next lesson we'll see how to snoop on Halide
    // at runtime.

    printf("Success!\n");
    return 0;
}
