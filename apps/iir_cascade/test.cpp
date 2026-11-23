#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "HalideBuffer.h"
#include "halide_benchmark.h"

#include "iir_cascade.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

// Must match the generator's sections parameter.
const int sections = 8;

// Coefficients for a cascade of peaking equalizers, with center frequencies
// spread across the audible range, alternating between boost and cut.
Buffer<float, 2> make_sos() {
    Buffer<float, 2> sos(6, sections);
    const double fs = 48000, q = 0.8;
    for (int k = 0; k < sections; k++) {
        double f0 = 120 * std::pow(8000.0 / 120, (double)k / (sections - 1));
        double gain = std::pow(10.0, ((k % 2) ? 3.0 : -3.0) / 40);
        double w0 = 2 * M_PI * f0 / fs;
        double alpha = std::sin(w0) / (2 * q);
        double a0 = 1 + alpha / gain;
        sos(0, k) = (1 + alpha * gain) / a0;
        sos(1, k) = -2 * std::cos(w0) / a0;
        sos(2, k) = (1 - alpha * gain) / a0;
        sos(3, k) = 1;
        sos(4, k) = -2 * std::cos(w0) / a0;
        sos(5, k) = (1 - alpha / gain) / a0;
    }
    return sos;
}

// Run the cascade on one channel in double precision.
std::vector<double> reference(const Buffer<float, 2> &input, const Buffer<float, 2> &sos, int c) {
    std::vector<double> x(input.dim(1).extent()), y(x.size());
    for (size_t n = 0; n < x.size(); n++) {
        x[n] = input(c, n);
    }
    for (int k = 0; k < sections; k++) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t n = 0; n < x.size(); n++) {
            y[n] = sos(0, k) * x[n] + sos(1, k) * x1 + sos(2, k) * x2 - sos(4, k) * y1 - sos(5, k) * y2;
            x2 = x1;
            x1 = x[n];
            y2 = y1;
            y1 = y[n];
        }
        std::swap(x, y);
    }
    return x;
}

int main(int argc, char **argv) {
    const int channels = 1024;
    const int samples = 65536;

    Buffer<float, 2> input(channels, samples);
    Buffer<float, 2> output(channels, samples);
    Buffer<float, 2> sos = make_sos();

    uint32_t seed = 0;
    input.for_each_value([&](float &v) {
        seed = seed * 1664525 + 1013904223;
        v = (int32_t)seed * (1.0f / 2147483648.0f);
    });

    double t = benchmark([&]() {
        iir_cascade(input, sos, output);
    });
    printf("Time: %gms\n", t * 1e3);

    for (int c = 0; c < channels; c += 37) {
        std::vector<double> expected = reference(input, sos, c);
        for (int n = 0; n < samples; n++) {
            if (std::abs(output(c, n) - expected[n]) > 1e-4) {
                printf("output(%d, %d) = %f instead of %f\n", c, n, output(c, n), expected[n]);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
