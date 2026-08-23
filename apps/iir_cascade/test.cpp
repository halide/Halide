#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "iir_cascade.h"
#include "iir_cascade_noninductive.h"

#include "halide_benchmark.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {

    const int T = 1024;  // number of time steps
    const int S = 1024;  // number of strips

    Halide::Runtime::Buffer<float> input(T, S);
    Halide::Runtime::Buffer<float> out_inductive(T, S);
    Halide::Runtime::Buffer<float> out_noninductive(T, S);

    // Set an element to something non-zero, to make sure the generators actually write to the output buffers.
    out_inductive(5, 5) = 1.f;
    out_noninductive(5, 5) = 2.f;

    input.for_each_element([&](int x, int y) {
        input(x, y) = 0.5f * x + 10.0f * sinf(0.01f * x + 0.02f * y);  // some arbitrary input signal
    });

    double t_inductive = benchmark([&]() {
        iir_cascade(input, out_inductive);
        out_inductive.device_sync();
    });
    printf("inductive time:     %gms\n", t_inductive * 1e3);

    double t_noninductive = benchmark([&]() {
        iir_cascade_noninductive(input, out_noninductive);
        out_noninductive.device_sync();
    });
    printf("non-inductive time: %gms\n", t_noninductive * 1e3);

    // out_inductive.copy_to_host();
    // out_noninductive.copy_to_host();

    float max_err = 0.f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < T; x++) {
            max_err = std::max(max_err, std::abs(out_inductive(x, y) - out_noninductive(x, y)));
        }
    }
    printf("max abs difference: %g\n", max_err);
    if (max_err > 1e-4f) {
        printf("Inductive and non-inductive outputs differ!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
