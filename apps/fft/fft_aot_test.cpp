#include <cmath>
#include <iostream>
#include <iomanip>

#include "generated/fft_forward_r2c.h"
#include "generated/fft_inverse_c2r.h"
#include "generated/fft_forward_c2c.h"
#include "generated/fft_inverse_c2c.h"

namespace {
const float kPi = 3.14159265358979310000f;

const size_t kSize = 16;
}

// Make a buffer_t for real input to the FFT.
buffer_t real_buffer(float *storage, int32_t y_size = kSize) {
    buffer_t buf = {0};

    buf.host = (uint8_t *)storage;
    buf.extent[0] = kSize;
    buf.stride[0] = 1;
    buf.extent[1] = y_size;
    buf.stride[1] = kSize;
    buf.elem_size = sizeof(float);

    return buf;
}

// Make a buffer_t for complex input to the FFT.
buffer_t complex_buffer(float *storage, int32_t y_size = kSize) {
    buffer_t buf = {0};

    buf.host = (uint8_t *)storage;
    buf.extent[0] = 2;
    buf.stride[0] = 1;
    buf.extent[1] = kSize;
    buf.stride[1] = 2;
    buf.extent[2] = y_size;
    buf.stride[2] = kSize * 2;
    buf.elem_size = sizeof(float);

    return buf;
}

int main(int argc, char **argv) {
    // Full size, complex, buffers. Not all of which will be used for some cases.
    float input[kSize * kSize * 2] = {0};
    float output[kSize * kSize * 2] = {0};

    std::cout << std::fixed << std::setprecision(2);

    // Forward real to complex test.
    {
        std::cout << "Forward real to complex test." << std::endl;

        float signal_1d[kSize];
        for (size_t i = 0; i < kSize; i++) {
            signal_1d[i] = 0;
            for (size_t k = 1; k < 5; k++) {
                signal_1d[i] += cos(2 * kPi * (k * (i / (float)kSize) + (k / 16.0f)));
            }
        }

        for (int j = 0; j < kSize; j++) {
            for (int i = 0; i < kSize; i++) {
                input[i + j * kSize] = signal_1d[i] + signal_1d[j];
            }
        }

        buffer_t in = real_buffer(input);
        buffer_t out = complex_buffer(output, kSize / 2 + 1);

        int halide_result;
        halide_result = fft_forward_r2c(&in, &out);

        if (halide_result != 0) {
            std::cerr << "fft_forward_r2c failed returning " << halide_result << std::endl;
            exit(1);
        }

        for (size_t i = 1; i < 5; i++) {
            // Check horizontal bins
            float real = output[i * 2];
            float imaginary = output[i * 2 + 1];
            float magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - .5f) > .001) {
                std::cerr << "fft_forward_r2c bad magnitude for horizontal bin " << i << ":" << magnitude << std::endl;
                exit(1);
            }
            float phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_r2c bad phase angle for horizontal bin " << i << ": " << phase_angle << std::endl;
                exit(1);
            }
            // Check vertical bins
            real = output[i * 2 * kSize];
            imaginary = output[i * 2 * kSize + 1];
            magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - .5f) > .001) {
                std::cerr << "fft_forward_r2c bad magnitude for vertical bin " << i << ":" << magnitude << std::endl;
                exit(1);
            }
            phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_r2c bad phase angle for vertical bin " << i << ": " << phase_angle << std::endl;
                exit(1);
            }
        }           

        // Check all other components are close to zero.
        for (size_t j = 0; j < kSize; j++) {
            for (size_t i = 0; i < kSize; i++) {
                // The first four non-DC bins in x and y have non-zero
                // values. The horizontal ones are mirrored into the
                // negative frequency components as well.
                if (!((j == 0 && ((i > 0 && i < 5) || (i > kSize - 5))) ||
                      (i == 0 && j > 0 && j < 5))) {
                    float real = output[(j * kSize + i) * 2];
                    float imaginary = output[(j * kSize + i) * 2 + 1];
                    if (fabs(real) > .001) {
                        std::cerr << "fft_forward_r2c real component at (" << i << ", " << j << ") is non-zero: " << real << std::endl;
                        exit(1);
                    }
                    if (fabs(imaginary) > .001) {
                        std::cerr << "fft_forward_r2c imaginary component at (" << i << ", " << j << ") is non-zero: " << imaginary << std::endl;
                        exit(1);
                    }
                }
            }
        }
    }

    // Inverse complex to real test.
    {
        std::cout << "Inverse complex to real test." << std::endl;

        memset(input, 0, sizeof(input));

        // There are four components that get summed to form the magnitude, which we want to be 1.
        // The components are each of the positive and negative frequencies and each of the
        // real and complex components. The +/- frequencies sum algebraically and the complex
        // components contribute to the magnitude as the sides of triangle like any 2D vector.
        float term_magnitude = 1.0f / (2.0f * sqrt(2.0f));
        input[2] = term_magnitude;
        input[3] = term_magnitude;
        // Negative frequencies count backward from end, no DC term
        input[(kSize - 1) * 2] = term_magnitude;
        input[(kSize - 1) * 2 + 1] = -term_magnitude; // complex conjugate

        buffer_t in = complex_buffer(input);
        buffer_t out = real_buffer(output);

        int halide_result;
        halide_result = fft_inverse_c2r(&in, &out);

        for (size_t j = 0; j < kSize; j++) {
            for (size_t i = 0; i < kSize; i++) {
                float sample = output[j * kSize + i];
                float expected = cos(2 * kPi * (i / 16.0f + .125f));
                if (fabs(sample - expected) > .001) {
                    std::cerr << "fft_inverse_c2r mismatch at (" << i << ", " << j << ") " << sample << " vs. " << expected << std::endl;
                    exit(1);
                }
            }
        }
    }

    // Forward complex to complex test.
    {
        std::cout << "Forward complex to complex test." << std::endl;

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
                input[(i + j * kSize) * 2] = signal_1d_real[i] + signal_1d_real[j];
                input[(i + j * kSize) * 2 + 1] = signal_1d_complex[i] + signal_1d_complex[j];
            }
        }

        buffer_t in = complex_buffer(input);
        buffer_t out = complex_buffer(output);

        int halide_result;
        halide_result = fft_forward_c2c(&in, &out);

        for (size_t i = 1; i < 5; i++) {
            // Check horizontal bins
            float real = output[i * 2];
            float imaginary = output[i * 2 + 1];
            float magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - 1.0f) > .001) {
                std::cerr << "fft_forward_c2c bad magnitude for horizontal bin " << i << ":" << magnitude << std::endl;
                exit(1);
            }
            float phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_c2c bad phase angle for horizontal bin " << i << ": " << phase_angle << std::endl;
                exit(1);
            }
            // Check vertical bins
            real = output[i * 2 * kSize];
            imaginary = output[i * 2 * kSize + 1];
            magnitude = sqrt(real * real + imaginary * imaginary);
            if (fabs(magnitude - 1.0f) > .001) {
                std::cerr << "fft_forward_c2c bad magnitude for vertical bin " << i << ":" << magnitude << std::endl;
                exit(1);
            }
            phase_angle = atan2(imaginary, real);
            if (fabs(phase_angle - (i / 16.0f) * 2 * kPi) > .001) {
                std::cerr << "fft_forward_c2c bad phase angle for vertical bin " << i << ": " << phase_angle << std::endl;
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
                    float real = output[(j * kSize + i) * 2];
                    float imaginary = output[(j * kSize + i) * 2 + 1];
                    if (fabs(real) > .001) {
                        std::cerr << "fft_forward_c2c real component at (" << i << ", " << j << ") is non-zero: " << real << std::endl;
                        exit(1);
                    }
                    if (fabs(imaginary) > .001) {
                        std::cerr << "fft_forward_c2c imaginary component at (" << i << ", " << j << ") is non-zero: " << imaginary << std::endl;
                        exit(1);
                    }
                }
            }
        }
    }

    // Inverse complex to complex test.
    {
        std::cout << "Inverse complex to complex test." << std::endl;

        memset(input, 0, sizeof(input));

        input[2] = .5f;
        input[3] = .5f;
        input[(kSize - 1) * 2] = .5f;
        input[(kSize - 1) * 2 + 1] = .5f; // Not conjugate. Result will not be real

        buffer_t in = complex_buffer(input);
        buffer_t out = complex_buffer(output);

        int halide_result;
        halide_result = fft_inverse_c2c(&in, &out);

        for (size_t j = 0; j < kSize; j++) {
            for (size_t i = 0; i < kSize; i++) {
                float real_sample = output[(j * kSize + i) * 2];
                float imaginary_sample = output[(j * kSize + i) * 2 + 1];
                float real_expected = 1 / sqrt(2) * (cos(2 * kPi * (i / 16.0f + .125)) + cos(2 * kPi * (i * (kSize - 1) / 16.0f + .125)));
                float imaginary_expected = 1 / sqrt(2) * (sin(2 * kPi * (i / 16.0f + .125)) +  sin(2 * kPi * (i * (kSize - 1) / 16.0f + .125)));

                if (fabs(real_sample - real_expected) > .001) {
                    std::cerr << "fft_inverse_c2c real mismatch at (" << i << ", " << j << ") " << real_sample << " vs. " << real_expected << std::endl;
                    exit(1);
                }

                if (fabs(imaginary_sample - imaginary_expected) > .001) {
                    std::cerr << "fft_inverse_c2c imaginary mismatch at (" << i << ", " << j << ") " << imaginary_sample << " vs. " << imaginary_expected << std::endl;
                    exit(1);
                }
            }
        }
    }

    exit(0);
}
