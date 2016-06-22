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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "clock.h"

buffer_t make_planar_rgb_image(int width, int height) {
    buffer_t buf = {0};
    // Allocate the memory required and clear it
    buf.host = (uint8_t *)malloc(width * height * 3);
    memset(buf.host, 0, width * height * 3);
    // This will be an 8-bit image.
    buf.elem_size = 1;

    // Set the sizes of each dimension
    buf.extent[0] = width;
    buf.extent[1] = height;
    // There are three color channels
    buf.extent[2] = 3;

    // Planar layout means that the stride in x is 1
    buf.stride[0] = 1;
    // The stride in y is the width of the image
    buf.stride[1] = width;
    // And the stride in c is width times height. This means that to
    // get to the next color channel, one should step forwards
    // width*height elements in memory.
    buf.stride[2] = width*height;

    return buf;
}

buffer_t make_interleaved_rgb_image(int width, int height) {
    buffer_t buf = {0};
    // This code will be the same as making a planar rgb image ...
    buf.host = (uint8_t *)malloc(width * height * 3);
    memset(buf.host, 0, width * height * 3);
    buf.elem_size = 1;
    buf.extent[0] = width;
    buf.extent[1] = height;
    buf.extent[2] = 3;

    // ... except we'll set up the strides differently.
    // Interleaved layout means that the stride in x is 3
    buf.stride[0] = 3;
    // The stride in y is three times the width of the image
    buf.stride[1] = 3*width;
    // And the stride in c is 1.
    buf.stride[2] = 1;

    return buf;
}

void destroy_image(buffer_t *b) {
    free(b->host);
    b->host = NULL;
}

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
    // memory, and buffer_ts that describe them.
    buffer_t planar_input = make_planar_rgb_image(1024, 768);
    buffer_t planar_output = make_planar_rgb_image(1024, 768);
    buffer_t interleaved_input = make_interleaved_rgb_image(1024, 768);
    buffer_t interleaved_output = make_interleaved_rgb_image(1024, 768);

    // We'll now call the various functions we compiled and check the
    // performance of each.
    uint8_t offset = 1;

    // Start the clock
    tick(NULL);

    // Run the planar version of the code on the planar images and the
    // interleaved version of the code on the interleaved
    // images. We'll run each 1000 times for benchmarking.
    for (int i = 0; i < 1000; i++) {
        brighten_planar(&planar_input, 1, &planar_output);
    }
    double planar_time = tick("brighten_planar");

    for (int i = 0; i < 1000; i++) {
        brighten_interleaved(&interleaved_input, 1, &interleaved_output);
    }
    double interleaved_time = tick("brighten_interleaved");

    // Planar is generally faster than interleaved for most imaging
    // operations.
    assert(planar_time < interleaved_time);

    // Either of these next two commented-out calls would throw an
    // error, because the stride is not what we promised it would be
    // in the generator.

    // brighten_planar(&interleaved_input, 1, &interleaved_output);
    // Error: Constraint violated: brighter.stride.0 (3) == 1 (1)

    // brighten_interleaved(&planar_input, 1, &planar_output);
    // Error: Constraint violated: brighter.stride.0 (1) == 3 (3)

    // Run the flexible version of the code and check performance. It
    // should work, but it'll be slower than the versions above.
    for (int i = 0; i < 1000; i++) {
        brighten_either(&planar_input, 1, &planar_output);
    }
    double either_planar_time = tick("brighten_either on planar images");
    assert(planar_time < either_planar_time);

    for (int i = 0; i < 1000; i++) {
        brighten_either(&interleaved_input, 1, &interleaved_output);
    }
    double either_interleaved_time = tick("brighten_either on interleaved images");
    assert(interleaved_time < either_interleaved_time);

    // Run the specialized version of the code on each layout. It
    // should match the performance of the code compiled specifically
    // for each case above by branching internally to equivalent
    // code.
    for (int i = 0; i < 1000; i++) {
        brighten_specialized(&planar_input, 1, &planar_output);
    }
    double specialized_planar_time = tick("brighten_specialized on planar images");

    // The cost of the if statement should be negligible, but we'll
    // allow a tolerance of 50% for this test to account for
    // measurement noise.
    assert(specialized_planar_time < 1.5 * planar_time);

    for (int i = 0; i < 1000; i++) {
        brighten_specialized(&interleaved_input, 1, &interleaved_output);
    }
    double specialized_interleaved_time = tick("brighten_specialized on interleaved images");
    assert(specialized_interleaved_time < 1.5 * interleaved_time);

    // Remember to free our allocated memory...
    destroy_image(&planar_input);
    destroy_image(&planar_output);
    destroy_image(&interleaved_input);
    destroy_image(&interleaved_output);

    return 0;
}
