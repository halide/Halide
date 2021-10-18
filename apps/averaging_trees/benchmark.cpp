#include "HalideBuffer.h"
#include "bin/bilinear_upsample_averaging.h"
#include "bin/bilinear_upsample_dither.h"
#include "bin/bilinear_upsample_float.h"
#include "bin/bilinear_upsample_float_dither.h"
#include "bin/bilinear_upsample_round_to_even.h"
#include "bin/bilinear_upsample_round_up.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

#include <random>

#define SZ 2048

struct Result {
    double time = 0, bias = 0, max_error = 0;

    void show(double megapixels) const {
        printf("Throughput: %0.3f mp/s Bias: %0.3f Max error: %0.3f\n",
               megapixels / time, bias, max_error);
    }
};

template<typename T>
void compute_bias_and_error(Halide::Runtime::Buffer<T> in,
                            Halide::Runtime::Buffer<T> out,
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

void compute_relative_bias_and_error(Halide::Runtime::Buffer<uint8_t> out,
                                     Halide::Runtime::Buffer<float> ground_truth,
                                     Result *r) {
    r->bias = r->max_error = 0;
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            double correct = ground_truth(x, y) * 256.0f;
            double diff = out(x, y) - correct;
            r->bias += diff;
            r->max_error = std::max(std::abs(diff), r->max_error);
        }
    }
    r->bias /= out.width() * out.height();
}

Halide::Runtime::Buffer<uint8_t> load_and_rescale_noise(const char *filename) {
    // We only need 4-bit noise
    Halide::Runtime::Buffer<uint8_t> noise = Halide::Tools::load_image(filename);
    noise.for_each_value([](uint8_t &x) { x >>= 4; });
    return noise;
}

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<uint8_t> in(SZ / 2 + 1, SZ / 2 + 1), out(SZ, SZ);
    Halide::Runtime::Buffer<float> in_float(SZ / 2 + 1, SZ / 2 + 1), out_float(SZ, SZ);

    std::mt19937 rng{0};
    in.for_each_value([&](uint8_t &x, float &y) { x = rng(); y = x; }, in_float);

    // Load some tileable blue noise for dithering from:
    // http://momentsingraphics.de/BlueNoise.html
    auto noise0 = load_and_rescale_noise("noise0.png");
    auto noise1 = load_and_rescale_noise("noise1.png");
    auto noise2 = load_and_rescale_noise("noise2.png");
    auto noise3 = load_and_rescale_noise("noise3.png");
    auto noise4 = load_and_rescale_noise("noise4.png");

    Result averaging, round_up, round_to_even, dither, float_;
    int count = 0;
    for (int i = 0; i < 10; i++) {
        averaging.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_averaging(in, noise0, out);
        });

        round_up.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_round_up(in, noise0, out);
        });

        round_to_even.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_round_to_even(in, noise0, out);
        });

        dither.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_dither(in, noise0, out);
        });

        float_.time += Halide::Tools::benchmark(10, 10, [&]() {
            bilinear_upsample_float(in_float, noise0, out_float);
        });
        count++;
        if (i == 1) {
            // Treat first two iterations as a warm-up
            averaging.time = round_up.time = round_to_even.time = dither.time = float_.time = 0;
            count = 0;
        }
    }

    round_up.time /= count;
    averaging.time /= count;
    round_to_even.time /= count;
    dither.time /= count;
    float_.time /= count;

    bilinear_upsample_round_up(in, noise0, out);
    compute_bias_and_error(in, out, &round_up);

    bilinear_upsample_averaging(in, noise0, out);
    compute_bias_and_error(in, out, &averaging);

    bilinear_upsample_round_to_even(in, noise0, out);
    compute_bias_and_error(in, out, &round_to_even);

    bilinear_upsample_dither(in, noise0, out);
    compute_bias_and_error(in, out, &dither);

    bilinear_upsample_float(in_float, noise0, out_float);
    compute_bias_and_error(in_float, out_float, &float_);

    printf("Results for single bilinear upsample from 1MP to 4MP:\n");

    printf("Averaging     ");
    averaging.show(4);
    printf("Round up      ");
    round_up.show(4);
    printf("Round to even ");
    round_to_even.show(4);
    printf("Dither        ");
    dither.show(4);
    printf("Float         ");
    float_.show(4);

    printf("Averaging is %2.2f%% faster than round up\n", 100 * (round_up.time - averaging.time) / round_up.time);

    // Bilinearly upsample a smooth ramp three times using the
    // different methods
    int sz = 1024;
    Halide::Runtime::Buffer<uint8_t> circle_in(sz, sz), circle0(sz, sz), circle1(sz, sz);
    Halide::Runtime::Buffer<float> float_circle_in(sz, sz), float_circle0(sz, sz), float_circle1(sz, sz);
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
                    accum += (fx * fx + fy * fy < 2.75 * 2.75) ? 1.0 : 0.0;
                }
            }
            double intensity = 1.0f - accum / 256.0;
            circle_in(x, y) = std::trunc(intensity * 5);
            float_circle_in(x, y) = circle_in(x, y) / 256.0f;
        }
    }

    printf("Results for upsampling a dot five times. Bias and error not\n"
           "particularly meaningful, because this is a specific structure, not\n"
           "random noise.\n");

    auto circ = circle_in.cropped(0, 0, 256).cropped(1, 0, 256);

    Halide::Tools::save_image(circ, "circle_input.png");
    circ = circle1.cropped(0, 0, 256).cropped(1, 0, 256);

    float_.time = Halide::Tools::benchmark(10, 10, [&]() {
        bilinear_upsample_float(float_circle_in, noise0, float_circle1);
        bilinear_upsample_float(float_circle1, noise1, float_circle0);
        bilinear_upsample_float(float_circle0, noise2, float_circle1);
        bilinear_upsample_float(float_circle1, noise3, float_circle0);
        bilinear_upsample_float_dither(float_circle0, noise4, circle1);
    });
    Halide::Tools::convert_and_save_image(circ, "circle_float.png");

    averaging.time = Halide::Tools::benchmark(10, 10, [&]() {
        bilinear_upsample_averaging(circle_in, noise0, circle1);
        bilinear_upsample_averaging(circle1, noise1, circle0);
        bilinear_upsample_averaging(circle0, noise2, circle1);
        bilinear_upsample_averaging(circle1, noise3, circle0);
        bilinear_upsample_averaging(circle0, noise4, circle1);
    });
    Halide::Tools::save_image(circ, "circle_averaging.png");
    compute_relative_bias_and_error(circ, float_circle1, &averaging);

    round_up.time = Halide::Tools::benchmark(10, 10, [&]() {
        bilinear_upsample_round_up(circle_in, noise0, circle1);
        bilinear_upsample_round_up(circle1, noise1, circle0);
        bilinear_upsample_round_up(circle0, noise2, circle1);
        bilinear_upsample_round_up(circle1, noise3, circle0);
        bilinear_upsample_round_up(circle0, noise4, circle1);
    });
    Halide::Tools::save_image(circ, "circle_round_up.png");
    compute_relative_bias_and_error(circ, float_circle1, &round_up);

    round_to_even.time = Halide::Tools::benchmark(10, 10, [&]() {
        bilinear_upsample_round_to_even(circle_in, noise0, circle1);
        bilinear_upsample_round_to_even(circle1, noise1, circle0);
        bilinear_upsample_round_to_even(circle0, noise2, circle1);
        bilinear_upsample_round_to_even(circle1, noise3, circle0);
        bilinear_upsample_round_to_even(circle0, noise4, circle1);
    });
    Halide::Tools::save_image(circ, "circle_round_to_even.png");
    compute_relative_bias_and_error(circ, float_circle1, &round_to_even);

    dither.time = Halide::Tools::benchmark(10, 10, [&]() {
        bilinear_upsample_dither(circle_in, noise0, circle1);
        bilinear_upsample_dither(circle1, noise1, circle0);
        bilinear_upsample_dither(circle0, noise2, circle1);
        bilinear_upsample_dither(circle1, noise3, circle0);
        bilinear_upsample_dither(circle0, noise4, circle1);
    });
    Halide::Tools::save_image(circ, "circle_dither.png");
    compute_relative_bias_and_error(circ, float_circle1, &dither);

    printf("Averaging     ");
    averaging.show(5);
    printf("Round up      ");
    round_up.show(5);
    printf("Round to even ");
    round_to_even.show(5);
    printf("Dither        ");
    dither.show(5);
    printf("Float         ");
    float_.show(5);

    return 0;
}
