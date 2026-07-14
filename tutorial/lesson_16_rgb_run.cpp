// Halide tutorial lesson 16: RGB images and memory layouts part 2

// Before reading this file, see lesson_16_rgb_generate.cpp

// This is the code that actually uses the Halide pipeline we've
// compiled. It does not depend on libHalide, so we won't be including
// Halide.h.
//
// Instead, it depends on the header files that lesson_16_rgb_generator produced.
#include "brighten_either.h"
#include "brighten_interleaved.h"
#include "brighten_planar.h"
#include "brighten_specialized.h"

// We'll use the Halide::Runtime::Buffer class for passing data into and out of
// the pipeline.
#include "HalideBuffer.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "halide_benchmark.h"

void check_timing(double faster, double slower) {
    if (faster > slower) {
        fprintf(stderr, "Warning: performance was worse than expected. %f should be less than %f\n", faster, slower);
    }
}

int main() {

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

    // Since we're about to compare the timings of six variants against each
    // other pairwise, we measure them all together with
    // Halide::Tools::benchmark_comparison() (see halide_benchmark.h). It
    // interleaves measurement across all of the operations passed to it
    // (instead of timing one fully and then the next), so that any
    // measurement noise -- CPU frequency scaling, thermal throttling, and so
    // on over the run -- is shared roughly equally across all six, rather
    // than systematically favoring whichever one happens to run first or
    // last. Each entry in the returned tuple is a BenchmarkResult; its
    // wall_time is a robust geometric mean of its average-iteration samples,
    // in seconds.
    auto [planar_result, interleaved_result,
          either_planar_result, either_interleaved_result,
          specialized_planar_result, specialized_interleaved_result] =
        Halide::Tools::benchmark_comparison(
            Halide::Tools::BenchmarkConfig{},
            // Run the planar version of the code on the planar images and
            // the interleaved version of the code on the interleaved images.
            [&]() { brighten_planar(planar_input, 1, planar_output); },
            [&]() { brighten_interleaved(interleaved_input, 1, interleaved_output); },
            // Either of these next two commented-out calls would throw an
            // error, because the stride is not what we promised it would be
            // in the generator.
            //
            // brighten_planar(interleaved_input, 1, interleaved_output);
            // Error: Constraint violated: brighter.stride.0 (3) == 1 (1)
            //
            // brighten_interleaved(planar_input, 1, planar_output);
            // Error: Constraint violated: brighter.stride.0 (1) == 3 (3)
            //
            // Run the flexible version of the code. It should work, but
            // it'll be slower than the versions above.
            [&]() { brighten_either(planar_input, 1, planar_output); },
            [&]() { brighten_either(interleaved_input, 1, interleaved_output); },
            // Run the specialized version of the code on each layout. It
            // should match the performance of the code compiled
            // specifically for each case above by branching internally to
            // equivalent code.
            [&]() { brighten_specialized(planar_input, 1, planar_output); },
            [&]() { brighten_specialized(interleaved_input, 1, interleaved_output); });

    double planar_time = planar_result.wall_time;
    double interleaved_time = interleaved_result.wall_time;
    double either_planar_time = either_planar_result.wall_time;
    double either_interleaved_time = either_interleaved_result.wall_time;
    double specialized_planar_time = specialized_planar_result.wall_time;
    double specialized_interleaved_time = specialized_interleaved_result.wall_time;

    printf("brighten_planar: %f msec\n", planar_time * 1000.f);
    printf("brighten_interleaved: %f msec\n", interleaved_time * 1000.f);

    // Planar is generally faster than interleaved for most imaging
    // operations.
    check_timing(planar_time, interleaved_time);

    printf("brighten_either on planar images: %f msec\n", either_planar_time * 1000.f);
    check_timing(planar_time, either_planar_time);

    printf("brighten_either on interleaved images: %f msec\n", either_interleaved_time * 1000.f);
    check_timing(interleaved_time, either_interleaved_time);

    printf("brighten_specialized on planar images: %f msec\n", specialized_planar_time * 1000.f);

    // The cost of the if statement should be negligible, but we'll
    // allow a tolerance of 50% for this test to account for
    // measurement noise.
    check_timing(specialized_planar_time, 1.5 * planar_time);

    printf("brighten_specialized on interleaved images: %f msec\n", specialized_interleaved_time * 1000.f);
    check_timing(specialized_interleaved_time, 2.0 * interleaved_time);

    return 0;
}
