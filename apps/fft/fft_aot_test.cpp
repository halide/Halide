#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string.h>

#include "HalideBuffer.h"

#include "fft_forward_c2c.h"
#include "fft_forward_r2c.h"
#include "fft_inverse_c2c.h"
#include "fft_inverse_c2r.h"

namespace {
const float kPi = 3.14159265358979310000f;

const int32_t kSize = 16;
}  // namespace

using Halide::Runtime::Buffer;

// Note that real_buffer() is 3D (with the 3rd dimension having extent 0)
// because the fft is written generically to require 3D inputs, even when they are real.
// Hence, the resulting buffer must be accessed with buf(i, j, 0).
Buffer<float, 3> real_buffer(int32_t y_size = kSize) {
    return Buffer<float, 3>::make_interleaved(kSize, y_size, 1);
}

Buffer<float, 3> complex_buffer(int32_t y_size = kSize) {
    return Buffer<float, 3>::make_interleaved(kSize, y_size, 2);
}

float &re(Buffer<float, 3> &b, int x, int y) {
    return b(x, y, 0);
}

float &im(Buffer<float, 3> &b, int x, int y) {
    return b(x, y, 1);
}

float re(const Buffer<float, 3> &b, int x, int y) {
    return b(x, y, 0);
}

float im(const Buffer<float, 3> &b, int x, int y) {
    return b(x, y, 1);
}

int main(int argc, char **argv) {
    std::cout << std::fixed << std::setprecision(2);

    // Forward real to complex test.
    {
        std::cout << "Forward real to complex test.\n";

        float signal_1d[kSize];
        for (size_t i = 0; i < kSize; i++) {
            signal_1d[i] = 0;
            for (size_t k = 1; k < 5; k++) {
                signal_1d[i] += cos(2 * kPi * (k * (i / (float)kSize) + (k / 16.0f)));
            }
        }

        auto in = real_buffer();
        for (int j = 0; j < kSize; j++) {
            for (int i = 0; i < kSize; i++) {
                in(i, j, 0) = signal_1d[i] + signal_1d[j];
            }
        }

        auto out = complex_buffer(kSize / 2 + 1);

        int halide_result;
        halide_result = fft_forward_r2c(in, out);
        if (halide_result != 0) {
            std::cerr << "fft_forward_r2c failed returning " << halide_result << "\n";
            exit(1);
        }

        for (size_t i = 1; i < 5; i++) {
            // Check horizontal bins
            float real = re(out, i, 0);
            float imaginary = im(out, i, 0);
            float magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - .5f) > .001) {
                std::cerr << "fft_forward_r2c bad magnitude for horizontal bin " << i << ":" << magnitude << "\n";
                exit(1);
            }
            float phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_r2c bad phase angle for horizontal bin " << i << ": " << phase_angle << "\n";
                exit(1);
            }
            // Check vertical bins
            real = re(out, 0, i);
            imaginary = im(out, 0, i);
            magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - .5f) > .001) {
                std::cerr << "fft_forward_r2c bad magnitude for vertical bin " << i << ":" << magnitude << "\n";
                exit(1);
            }
            phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_r2c bad phase angle for vertical bin " << i << ": " << phase_angle << "\n";
                exit(1);
            }
        }

        // Check all other components are close to zero.
        for (size_t j = 0; j < kSize / 2 + 1; j++) {
            for (size_t i = 0; i < kSize; i++) {
                // The first four non-DC bins in x and y have non-zero
                // values. The horizontal ones are mirrored into the
                // negative frequency components as well.
                if (!((j == 0 && ((i > 0 && i < 5) || (i > kSize - 5))) ||
                      (i == 0 && j > 0 && j < 5))) {
                    float real = re(out, i, j);
                    float imaginary = im(out, i, j);
                    if (fabs(real) > .001) {
                        std::cerr << "fft_forward_r2c real component at (" << i << ", " << j << ") is non-zero: " << real << "\n";
                        exit(1);
                    }
                    if (fabs(imaginary) > .001) {
                        std::cerr << "fft_forward_r2c imaginary component at (" << i << ", " << j << ") is non-zero: " << imaginary << "\n";
                        exit(1);
                    }
                }
            }
        }
    }

    // Inverse complex to real test.
    {
        std::cout << "Inverse complex to real test.\n";

        auto in = complex_buffer();
        in.fill(0);

        // There are four components that get summed to form the magnitude, which we want to be 1.
        // The components are each of the positive and negative frequencies and each of the
        // real and complex components. The +/- frequencies sum algebraically and the complex
        // components contribute to the magnitude as the sides of triangle like any 2D vector.
        float term_magnitude = 1.0f / (2.0f * sqrt(2.0f));
        re(in, 1, 0) = term_magnitude;
        im(in, 1, 0) = term_magnitude;
        // Negative frequencies count backward from end, no DC term
        re(in, kSize - 1, 0) = term_magnitude;
        im(in, kSize - 1, 0) = -term_magnitude;  // complex conjugate

        auto out = real_buffer();

        int halide_result;
        halide_result = fft_inverse_c2r(in, out);
        if (halide_result != 0) {
            std::cerr << "fft_inverse_c2r failed returning " << halide_result << "\n";
            exit(1);
        }

        for (size_t j = 0; j < kSize; j++) {
            for (size_t i = 0; i < kSize; i++) {
                float sample = out(i, j, 0);
                float expected = cos(2 * kPi * (i / 16.0f + .125f));
                if (fabs(sample - expected) > .001) {
                    std::cerr << "fft_inverse_c2r mismatch at (" << i << ", " << j << ") " << sample << " vs. " << expected << "\n";
                    exit(1);
                }
            }
        }
    }

    // Forward complex to complex test.
    {
        std::cout << "Forward complex to complex test.\n";

        auto in = complex_buffer();

        float signal_1d_real[kSize];
        float signal_1d_complex[kSize];
        for (size_t i = 0; i < kSize; i++) {
            signal_1d_real[i] = 0;
            signal_1d_complex[i] = 0;
            for (size_t k = 1; k < 5; k++) {
                signal_1d_real[i] += cos(2 * kPi * (k * (i / (float)kSize) + (k / 16.0f)));
                signal_1d_complex[i] += sin(2 * kPi * (k * (i / (float)kSize) + (k / 16.0f)));
            }
        }

        for (int j = 0; j < kSize; j++) {
            for (int i = 0; i < kSize; i++) {
                re(in, i, j) = signal_1d_real[i] + signal_1d_real[j];
                im(in, i, j) = signal_1d_complex[i] + signal_1d_complex[j];
            }
        }

        auto out = complex_buffer();

        int halide_result;
        halide_result = fft_forward_c2c(in, out);
        if (halide_result != 0) {
            std::cerr << "fft_forward_c2c failed returning " << halide_result << "\n";
            exit(1);
        }

        for (size_t i = 1; i < 5; i++) {
            // Check horizontal bins
            float real = re(out, i, 0);
            float imaginary = im(out, i, 0);
            float magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - 1.0f) > .001) {
                std::cerr << "fft_forward_c2c bad magnitude for horizontal bin " << i << ":" << magnitude << "\n";
                exit(1);
            }
            float phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_c2c bad phase angle for horizontal bin " << i << ": " << phase_angle << "\n";
                exit(1);
            }
            // Check vertical bins
            real = re(out, 0, i);
            imaginary = im(out, 0, i);
            magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - 1.0f) > .001) {
                std::cerr << "fft_forward_c2c bad magnitude for vertical bin " << i << ":" << magnitude << "\n";
                exit(1);
            }
            phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_c2c bad phase angle for vertical bin " << i << ": " << phase_angle << "\n";
                exit(1);
            }
        }

        // Check all other components are close to zero.
        for (size_t j = 0; j < kSize; j++) {
            for (size_t i = 0; i < kSize; i++) {
                // The first four non-DC bins in x and y have non-zero
                // values. The input is chose so the mirrored negative
                // frequency components are all zero due to
                // interference of the real and complex parts.
                if (!((j == 0 && (i > 0 && i < 5)) ||
                      (i == 0 && j > 0 && j < 5))) {
                    float real = re(out, i, j);
                    float imaginary = im(out, i, j);
                    if (fabs(real) > .001) {
                        std::cerr << "fft_forward_c2c real component at (" << i << ", " << j << ") is non-zero: " << real << "\n";
                        exit(1);
                    }
                    if (fabs(imaginary) > .001) {
                        std::cerr << "fft_forward_c2c imaginary component at (" << i << ", " << j << ") is non-zero: " << imaginary << "\n";
                        exit(1);
                    }
                }
            }
        }
    }

    // Inverse complex to complex test.
    {
        std::cout << "Inverse complex to complex test.\n";

        auto in = complex_buffer();
        in.fill(0);

        re(in, 1, 0) = .5f;
        im(in, 1, 0) = .5f;
        re(in, kSize - 1, 0) = .5f;
        im(in, kSize - 1, 0) = .5f;  // Not conjugate. Result will not be real

        auto out = complex_buffer();

        int halide_result;
        halide_result = fft_inverse_c2c(in, out);
        if (halide_result != 0) {
            std::cerr << "fft_inverse_c2c failed returning " << halide_result << "\n";
            exit(1);
        }

        for (size_t j = 0; j < kSize; j++) {
            for (size_t i = 0; i < kSize; i++) {
                float real_sample = re(out, i, j);
                float imaginary_sample = im(out, i, j);
                float real_expected = 1 / sqrt(2) * (cos(2 * kPi * (i / 16.0f + .125)) + cos(2 * kPi * (i * (kSize - 1) / 16.0f + .125)));
                float imaginary_expected = 1 / sqrt(2) * (sin(2 * kPi * (i / 16.0f + .125)) + sin(2 * kPi * (i * (kSize - 1) / 16.0f + .125)));

                if (fabs(real_sample - real_expected) > .001) {
                    std::cerr << "fft_inverse_c2c real mismatch at (" << i << ", " << j << ") " << real_sample << " vs. " << real_expected << "\n";
                    exit(1);
                }

                if (fabs(imaginary_sample - imaginary_expected) > .001) {
                    std::cerr << "fft_inverse_c2c imaginary mismatch at (" << i << ", " << j << ") " << imaginary_sample << " vs. " << imaginary_expected << "\n";
                    exit(1);
                }
            }
        }
    }

    std::cout << "Success!\n";
    exit(0);
}
