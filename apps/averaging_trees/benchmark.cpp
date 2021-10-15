#include "HalideBuffer.h"
#include "bin/bilinear_upsample_averaging.h"
#include "bin/bilinear_upsample_dither.h"
#include "bin/bilinear_upsample_round_to_even.h"
#include "bin/bilinear_upsample_round_up.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

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

    Result averaging, round_up, round_to_even, dither;
    int count = 0;
    for (int i = 0; i < 10; i++) {
        averaging.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_averaging(in, 0, out);
        });

        round_up.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_round_up(in, 0, out);
        });

        round_to_even.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_round_to_even(in, 0, out);
        });

        dither.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_dither(in, 0, out);
        });
        count++;
        if (i == 1) {
            // Treat first two iterations as a warm-up
            averaging.time = round_up.time = round_to_even.time = dither.time = 0;
            count = 0;
        }
    }

    round_up.time /= count;
    averaging.time /= count;
    round_to_even.time /= count;
    dither.time /= count;

    bilinear_upsample_round_up(in, 0, out);
    compute_bias_and_error(in, out, &round_up);

    bilinear_upsample_averaging(in, 0, out);
    compute_bias_and_error(in, out, &averaging);

    bilinear_upsample_round_to_even(in, 0, out);
    compute_bias_and_error(in, out, &round_to_even);

    bilinear_upsample_dither(in, 0, out);
    compute_bias_and_error(in, out, &dither);

    printf("Averaging     ");
    averaging.show();
    printf("Round up      ");
    round_up.show();
    printf("Round to even ");
    round_to_even.show();
    printf("Dither        ");
    dither.show();

    printf("Averaging is %2.2f%% faster than round up\n", 100 * (round_up.time - averaging.time) / round_up.time);

    // Bilinearly upsample a smooth ramp three times using the
    // different methods
    int sz = 256;
    Halide::Runtime::Buffer<uint8_t> circle_in(sz, sz), circle0(sz, sz), circle1(sz, sz);
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            // Super sample using a box
            double accum = 0;
            for (int dy = 0; dy < 16; dy++) {
                for (int dx = 0; dx < 16; dx++) {
                    double fx = x + dx / 16.0f + 1 / 32.0f;
                    double fy = y + dy / 16.0f + 1 / 32.0f;
                    fx -= 5;
                    fy -= 5;
                    accum += (fx * fx + fy * fy < 2.5 * 2.5) ? 1.0 : 0.0;
                }
            }
            double intensity = accum / 256.0;
            circle_in(x, y) = std::trunc(intensity * 5);
        }
    }

    Halide::Tools::save_image(circle_in, "circle_input.png");

    bilinear_upsample_averaging(circle_in, 0, circle1);
    bilinear_upsample_averaging(circle1, 1, circle0);
    bilinear_upsample_averaging(circle0, 2, circle1);
    bilinear_upsample_averaging(circle1, 3, circle0);
    bilinear_upsample_averaging(circle0, 4, circle1);
    Halide::Tools::save_image(circle1, "circle_averaging.png");

    bilinear_upsample_round_up(circle_in, 0, circle1);
    bilinear_upsample_round_up(circle1, 1, circle0);
    bilinear_upsample_round_up(circle0, 2, circle1);
    bilinear_upsample_round_up(circle1, 3, circle0);
    bilinear_upsample_round_up(circle0, 4, circle1);
    Halide::Tools::save_image(circle1, "circle_round_up.png");

    bilinear_upsample_round_to_even(circle_in, 0, circle1);
    bilinear_upsample_round_to_even(circle1, 1, circle0);
    bilinear_upsample_round_to_even(circle0, 2, circle1);
    bilinear_upsample_round_to_even(circle1, 3, circle0);
    bilinear_upsample_round_to_even(circle0, 4, circle1);
    Halide::Tools::save_image(circle1, "circle_round_to_even.png");

    bilinear_upsample_dither(circle_in, 0, circle1);
    bilinear_upsample_dither(circle1, 1, circle0);
    bilinear_upsample_dither(circle0, 2, circle1);
    bilinear_upsample_dither(circle1, 3, circle0);
    bilinear_upsample_dither(circle0, 4, circle1);
    Halide::Tools::save_image(circle1, "circle_dither.png");

    return 0;
}
