// Halide tutorial lesson 6: Realizing Funcs over arbitrary domains

// This lesson demonstrates how to evaluate a Func over a domain that
// does not start at (0, 0).

// On linux, you can compile and run it like so:
// g++ lesson_06*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_06 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_06

// On os x:
// g++ lesson_06*.cpp -g -I ../include -L ../bin -lHalide -o lesson_06 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_06

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_06_realizing_over_shifted_domains
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // The last lesson was quite involved, and scheduling complex
    // multi-stage pipelines is ahead of us. As an interlude, let's
    // consider something easy: evaluating funcs over rectangular
    // domains that do not start at the origin.

    // We define our familiar gradient function.
    Func gradient("gradient");
    Var x("x"), y("y");
    gradient(x, y) = x + y;

    // And turn on tracing so we can see how it is being evaluated.
    gradient.trace_stores();

    // Previously we've realized gradient like so:
    //
    // gradient.realize(8, 8);
    //
    // This does three things internally:
    // 1) Generates code than can evaluate gradient over an arbitrary
    // rectangle.
    // 2) Allocates a new 8 x 8 image.
    // 3) Runs the generated code to evaluate gradient for all x, y
    // from (0, 0) to (7, 7) and puts the result into the image.
    // 4) Returns the new image as the result of the realize call.

    // What if we're managing memory carefully and don't want Halide
    // to allocate a new image for us? We can call realize another
    // way. We can pass it an image we would like it to fill in. The
    // following evaluates our Func into an existing image:
    printf("Evaluating gradient from (0, 0) to (7, 7)\n");
    Buffer<int> result(8, 8);
    gradient.realize(result);

    // Let's check it did what we expect:
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (result(x, y) != x + y) {
                printf("Something went wrong!\n");
                return -1;
            }
        }
    }

    // Now let's evaluate gradient over a 5 x 7 rectangle that starts
    // somewhere else -- at position (100, 50). So x and y will run
    // from (100, 50) to (104, 56) inclusive.

    // We start by creating an image that represents that rectangle:
    Buffer<int> shifted(5, 7); // In the constructor we tell it the size.
    shifted.set_min(100, 50); // Then we tell it the top-left corner.

    printf("Evaluating gradient from (100, 50) to (104, 56)\n");

    // Note that this won't need to compile any new code, because when
    // we realized it the first time, we generated code capable of
    // evaluating gradient over an arbitrary rectangle.
    gradient.realize(shifted);

    // From C++, we also access the image object using coordinates
    // that start at (100, 50).
    for (int y = 50; y < 57; y++) {
        for (int x = 100; x < 105; x++) {
            if (shifted(x, y) != x + y) {
                printf("Something went wrong!\n");
                return -1;
            }
        }
    }
    // The image 'shifted' stores the value of our Func over a domain
    // that starts at (100, 50), so asking for shifted(0, 0) would in
    // fact read out-of-bounds and probably crash.

    // What if we want to evaluate our Func over some region that
    // isn't rectangular? Too bad. Halide only does rectangles :)

    printf("Success!\n");
    return 0;
}
