// Halide tutorial lesson 4

// This lesson demonstrates how to get Halide to call back into your C code.

// On linux, you can compile and run it like so:

// Note that we need to add -rdynamic as a compilation flag, so that
// Halide is able to sniff for your C function by name.

// g++ lesson_04.cpp -I ../include -L ../bin -rdynamic -lHalide -lpthread -ldl -o lesson_04
// LD_LIBRARY_PATH=../bin ./lesson_04

// On os x:
// g++ lesson_04.cpp -I ../include -L ../bin -lHalide -o lesson_04
// DYLD_LIBRARY_PATH=../bin ./lesson_04

#include <Halide.h>
#include <stdio.h>
using namespace Halide;

// We'll define a function that we want our Halide routine to call. It
// prints out a message and returns the third argument unchanged. It
// has to have 'extern "C"' linkage, so that Halide can look for it by
// name. C++ linkage results in name mangling and makes it impossible
// for Halide to find your function.
extern "C" int snoop(int x, int y, int val) {
    printf("Storing the value %d at pixel %d %d\n", val, x, y);
    return val;
}

// This macro creates a new Halide wrapper for the function, so we can
// call it using Exprs instead of ints. We pass it the return type,
// the name of the function, and the types of the arguments. The '_3'
// is because this is a 3-argument function. You should change this if
// you use more or fewer arguments.
HalideExtern_3(int, snoop, int, int, int);

int main(int argc, char **argv) {

    Func gradient("gradient");
    Var x("x"), y("y");

    // We'll call into our C function from the halide pipeline.
    gradient(x, y) = snoop(x, y, x + y);

    // Realize the function over an 8x8 region.
    printf("Evaluating gradient\n");
    Image<int> output = gradient.realize(8, 8);

    // This will print out all the times gradient(x, y) gets
    // evaluated. If it fails to compile, make sure you remembered to
    // include -rdynamic in the compiler flags.

    // Now that we can snoop on what Halide is doing, let's try our
    // first scheduling primitive. We'll make a new version of
    // gradient that processes each scanline in parallel.
    Func parallel_gradient;
    parallel_gradient(x, y) = snoop(x, y, x+y);

    // Things are the same so far. We've defined the algorithm, but
    // haven't said anything about how to schedule it. In general,
    // trying different scheduling decisions doesn't change the code
    // that describes the algorithm.

    // No we tell Halide to use a parallel for loop over the y
    // coordinate. On linux we run this using a thread pool and a task
    // queue. On os x we call into grand central dispatch, which does
    // the same thing for us.
    parallel_gradient.parallel(y);

    // This time the printfs should come out of order.
    printf("\nEvaluating parallel_gradient\n");
    parallel_gradient.realize(8, 8);
    
    printf("Success!\n");
    return 0;
}
