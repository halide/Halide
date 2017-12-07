// Halide tutorial lesson 16: RGB images and memory layouts part 2

// Before reading this file, see lesson_16_rgb_generate.cpp

// This is the code that actually uses the Halide pipeline we've
// compiled. It does not depend on libHalide, so we won't be including
// Halide.h.
//
// Instead, it depends on the header files that lesson_16_rgb_generator produced.
#include "brighten_planar.h"
#include "brighten_interleaved.h"
#include "brighten_either.h"
#include "brighten_specialized.h"

// We'll use the Halide::Runtime::Buffer class for passing data into and out of
// the pipeline.
#include "HalideBuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "clock.h"

// A function to help us report runtimes of various things. Prints the
// time elapsed since the last time you called this function.
double tick(const char *name) {
    static double t_old = 0;
    double t_new = current_time();
    double dt = t_new - t_old;
    if (name) {
        printf("%s: %f\n", name, dt);
    }
    t_old = t_new;
    return dt;
}

int main(int argc, char **argv) {

    // Let's make some images stored with interleaved and planar
    // memory. Halide::Runtime::Buffer is planar by default.
    Halide::Runtime::Buffer<uint8_t> planar_input(1024, 768, 3);
    Halide::Runtime::Buffer<uint8_t> planar_output(1024, 768, 3);
    Halide::Runtime::Buffer<uint8_t> interleaved_input =
        Halide::Runtime::Buffer<uint8_t>::make_interleaved(1024, 768, 3);
    Halide::Runtime::Buffer<uint8_t> interleaved_output =
        Halide::Runtime::Buffer<uint8_t>::make_interleaved(1024, 768, 3);

    // Let's check the strides are what we expect, given the
    // constraints we set up in the generator.
    assert(planar_input.dim(0).stride() == 1);
    assert(planar_output.dim(0).stride() == 1);
    assert(interleaved_input.dim(0).stride() == 3);
    assert(interleaved_output.dim(0).stride() == 3);
    assert(interleaved_input.dim(2).stride() == 1);
    assert(interleaved_output.dim(2).stride() == 1);

    // We'll now call the various functions we compiled and check the
    // performance of each.

    // Start the clock
    tick(NULL);

    // Run the planar version of the code on the planar images and the
    // interleaved version of the code on the interleaved
    // images. We'll run each 1000 times for benchmarking.
    for (int i = 0; i < 1000; i++) {
        brighten_planar(planar_input, 1, planar_output);
    }
    double planar_time = tick("brighten_planar");

    for (int i = 0; i < 1000; i++) {
        brighten_interleaved(interleaved_input, 1, interleaved_output);
    }
    double interleaved_time = tick("brighten_interleaved");

    // Planar is generally faster than interleaved for most imaging
    // operations.
    assert(planar_time < interleaved_time);

    // Either of these next two commented-out calls would throw an
    // error, because the stride is not what we promised it would be
    // in the generator.

    // brighten_planar(interleaved_input, 1, interleaved_output);
    // Error: Constraint violated: brighter.stride.0 (3) == 1 (1)

    // brighten_interleaved(planar_input, 1, planar_output);
    // Error: Constraint violated: brighter.stride.0 (1) == 3 (3)

    // Run the flexible version of the code and check performance. It
    // should work, but it'll be slower than the versions above.
    for (int i = 0; i < 1000; i++) {
        brighten_either(planar_input, 1, planar_output);
    }
    double either_planar_time = tick("brighten_either on planar images");
    assert(planar_time < either_planar_time);

    for (int i = 0; i < 1000; i++) {
        brighten_either(interleaved_input, 1, interleaved_output);
    }
    double either_interleaved_time = tick("brighten_either on interleaved images");
    assert(interleaved_time < either_interleaved_time);

    // Run the specialized version of the code on each layout. It
    // should match the performance of the code compiled specifically
    // for each case above by branching internally to equivalent
    // code.
    for (int i = 0; i < 1000; i++) {
        brighten_specialized(planar_input, 1, planar_output);
    }
    double specialized_planar_time = tick("brighten_specialized on planar images");

    // The cost of the if statement should be negligible, but we'll
    // allow a tolerance of 50% for this test to account for
    // measurement noise.
    assert(specialized_planar_time < 1.5 * planar_time);

    for (int i = 0; i < 1000; i++) {
        brighten_specialized(interleaved_input, 1, interleaved_output);
    }
    double specialized_interleaved_time = tick("brighten_specialized on interleaved images");
    assert(specialized_interleaved_time < 2.0 * interleaved_time);

    return 0;
}
