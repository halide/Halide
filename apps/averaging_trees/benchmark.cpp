#include "HalideBuffer.h"
#include "bin/bilinear_upsample_averaging.h"
#include "bin/bilinear_upsample_round_up.h"
#include "halide_benchmark.h"

#include <random>

#define SZ 2048

struct Result {
    double time = 0, bias = 0, max_error = 0;

    void show() const {
        printf("Time: %d us Bias: %0.3f Max error: %0.3f\n",
               (int)(time * 1e6), bias, max_error);
    }
};

void compute_bias_and_error(Halide::Runtime::Buffer<uint8_t> in,
                            Halide::Runtime::Buffer<uint8_t> out,
                            Result *r) {
    r->bias = r->max_error = 0;
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            double correct = 0;
            int xo = x >> 1, yo = y >> 1;
            int xi = x & 1, yi = y & 1;
            correct += in(xo + xi, yo + yi) * 9.0 / 16;
            correct += in(xo + 1 - xi, yo + yi) * 3.0 / 16;
            correct += in(xo + xi, yo + 1 - yi) * 3.0 / 16;
            correct += in(xo + 1 - xi, yo + 1 - yi) * 1.0 / 16;

            double diff = out(x, y) - correct;
            r->bias += diff;
            r->max_error = std::max(std::abs(diff), r->max_error);
        }
    }
    r->bias /= out.width() * out.height();
}

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<uint8_t> in(SZ / 2 + 1, SZ / 2 + 1), out(SZ, SZ);

    std::mt19937 rng{0};
    in.for_each_value([&](uint8_t &x) { x = rng(); });

    Result averaging, round_up;
    int count = 0;
    for (int i = 0; averaging.time + round_up.time < 0.1; i++) {
        averaging.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_averaging(in, out);
        });

        round_up.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_round_up(in, out);
        });
        count++;
        if (i == 1) {
            // Treat first two iterations as a warm-up
            averaging.time = round_up.time = 0;
            count = 0;
        }
    }

    round_up.time /= count;
    averaging.time /= count;

    bilinear_upsample_round_up(in, out);
    compute_bias_and_error(in, out, &round_up);

    bilinear_upsample_averaging(in, out);
    compute_bias_and_error(in, out, &averaging);

    printf("Round-up ");
    round_up.show();
    printf("Averaging ");
    averaging.show();

    printf("Averaging is %2.2f%% faster\n", 100 * (round_up.time - averaging.time) / round_up.time);

    // Compute bias and error

    return 0;
}
