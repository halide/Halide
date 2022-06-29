// to run:
// $ clang example1.cpp -o example1 -O2 --std=c++17 -lstdc++ -I/opt/homebrew/include -L/opt/homebrew/lib -lHalide
// $ ./example1

// The only Halide header file you need is Halide.h. It includes all of Halide.
#include "Halide.h"

// We'll also include stdio for printf.
#include <stdio.h>
// #include "halide_image_io.h"

#include "../src/StmtCost.h"

using namespace Halide;

// example for testing StmtCost.cpp
Func example_fixed(Halide::Buffer<uint8_t> input) {

    // Next we define our Func object that represents our one pipeline
    // stage.
    Halide::Func brighter("brighter");

    // Our Func will have three arguments, representing the position
    // in the image and the color channel. Halide treats color
    // channels as an extra dimension of the image.
    Halide::Var x("x"), y("y"), c("c");

    // Normally we'd probably write the whole function definition on
    // one line. Here we'll break it apart so we can explain what
    // we're doing at every step.

    // For each pixel of the input image.
    Halide::Expr value = input(x, y, c);

    // Cast it to a floating point value.
    value = Halide::cast<float>(value);

    // Multiply it by 1.5 to brighten it. Halide represents real
    // numbers as floats, not doubles, so we stick an 'f' on the end
    // of our constant.
    value = value * 1.5f;

    // Clamp it to be less than 255, so we don't get overflow when we
    // cast it back to an 8-bit unsigned int.
    value = Halide::min(value, 255.0f);

    // Cast it back to an 8-bit unsigned integer.
    value = Halide::cast<uint8_t>(value);

    // Define the function.
    brighter(x, y, c) = value;

    return brighter;
}

int main(int argc, char **argv) {

    // TESTING - Darya
    // Halide::Buffer<uint8_t> input = load_image("images/rgb.png");

    // Func myFunc = example_fixed(input);

    FindStmtCost cost;
    // cost.visit(myFunc);

    // Everything worked! We defined a Func, then called 'realize' on
    // it to generate and run machine code that produced an Buffer.
    printf("Success!\n");

    return 0;
}
