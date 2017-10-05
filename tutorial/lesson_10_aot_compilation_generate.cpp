// Halide tutorial lesson 10: AOT compilation part 1

// This lesson demonstrates how to use Halide as an more traditional
// ahead-of-time (AOT) compiler.

// This lesson is split across two files. The first (this one), builds
// a Halide pipeline and compiles it to a static library and
// header. The second (lesson_10_aot_compilation_run.cpp), uses that
// static library to actually run the pipeline. This means that
// compiling this code is a multi-step process.

// On linux, you can compile and run it like so:
// g++ lesson_10*generate.cpp -g -std=c++11 -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_10_generate
// LD_LIBRARY_PATH=../bin ./lesson_10_generate
// g++ lesson_10*run.cpp lesson_10_halide.a -std=c++11 -I ../include -lpthread -ldl -o lesson_10_run
// ./lesson_10_run

// On os x:
// g++ lesson_10*generate.cpp -g -std=c++11 -I ../include -L ../bin -lHalide -o lesson_10_generate
// DYLD_LIBRARY_PATH=../bin ./lesson_10_generate
// g++ lesson_10*run.cpp lesson_10_halide.a -o lesson_10_run -I ../include
// ./lesson_10_run

// The benefits of this approach are that the final program:
// - Doesn't do any jit compilation at runtime, so it's fast.
// - Doesn't depend on libHalide at all, so it's a small, easy-to-deploy binary.

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_10_aot_compilation_run
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>
using namespace Halide;

int main(int argc, char **argv) {

    // We'll define a simple one-stage pipeline:
    Func brighter;
    Var x, y;

    // The pipeline will depend on one scalar parameter.
    Param<uint8_t> offset;

    // And take one grayscale 8-bit input buffer. The first
    // constructor argument gives the type of a pixel, and the second
    // specifies the number of dimensions (not the number of
    // channels!). For a grayscale image this is two; for a color
    // image it's three. Currently, four dimensions is the maximum for
    // inputs and outputs.
    ImageParam input(type_of<uint8_t>(), 2);

    // If we were jit-compiling, these would just be an int and a
    // Buffer, but because we want to compile the pipeline once and
    // have it work for any value of the parameter, we need to make a
    // Param object, which can be used like an Expr, and an ImageParam
    // object, which can be used like a Buffer.

    // Define the Func.
    brighter(x, y) = input(x, y) + offset;

    // Schedule it.
    brighter.vectorize(x, 16).parallel(y);

    // This time, instead of calling brighter.realize(...), which
    // would compile and run the pipeline immediately, we'll call a
    // method that compiles the pipeline to a static library and header.
    //
    // For AOT-compiled code, we need to explicitly declare the
    // arguments to the routine. This routine takes two. Arguments are
    // usually Params or ImageParams.
    brighter.compile_to_static_library("lesson_10_halide", {input, offset}, "brighter");

    printf("Halide pipeline compiled, but not yet run.\n");

    // To continue this lesson, look in the file lesson_10_aot_compilation_run.cpp

    return 0;
}
