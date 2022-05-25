#include <cmath>
#include <cstdint>
#include <cstdio>
#ifdef __SSE2__
#include <emmintrin.h>
#elif __ARM_NEON
#include <arm_neon.h>
#endif

#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

double t;

Buffer<uint16_t, 2> blur(Buffer<uint16_t, 2> in) {
    Buffer<uint16_t, 2> tmp(in.width() - 8, in.height());
    Buffer<uint16_t, 2> out(in.width() - 8, in.height() - 2);

    t = benchmark(10, 1, [&]() {
        for (int y = 0; y < tmp.height(); y++)
            for (int x = 0; x < tmp.width(); x++)
                tmp(x, y) = (in(x, y) + in(x + 1, y) + in(x + 2, y)) / 3;

        for (int y = 0; y < out.height(); y++)
            for (int x = 0; x < out.width(); x++)
                out(x, y) = (tmp(x, y) + tmp(x, y + 1) + tmp(x, y + 2)) / 3;
    });

    return out;
}

Buffer<uint16_t, 2> blur_fast(Buffer<uint16_t, 2> in) {
    Buffer<uint16_t, 2> out(in.width() - 8, in.height() - 2);

    t = benchmark(10, 1, [&]() {
#ifdef __SSE2__
        __m128i one_third = _mm_set1_epi16(21846);
#pragma omp parallel for
        for (int yTile = 0; yTile < out.height(); yTile += 32) {
            __m128i tmp[(128 / 8) * (32 + 2)];
            for (int xTile = 0; xTile < out.width(); xTile += 128) {
                __m128i *tmpPtr = tmp;
                for (int y = 0; y < 32 + 2; y++) {
                    const uint16_t *inPtr = &(in(xTile, yTile + y));
                    for (int x = 0; x < 128; x += 8) {
                        __m128i a = _mm_load_si128((const __m128i *)(inPtr));
                        __m128i b = _mm_loadu_si128((const __m128i *)(inPtr + 1));
                        __m128i c = _mm_loadu_si128((const __m128i *)(inPtr + 2));
                        __m128i sum = _mm_add_epi16(_mm_add_epi16(a, b), c);
                        __m128i avg = _mm_mulhi_epi16(sum, one_third);
                        _mm_store_si128(tmpPtr++, avg);
                        inPtr += 8;
                    }
                }
                tmpPtr = tmp;
                for (int y = 0; y < 32; y++) {
                    __m128i *outPtr = (__m128i *)(&(out(xTile, yTile + y)));
                    for (int x = 0; x < 128; x += 8) {
                        __m128i a = _mm_load_si128(tmpPtr + (2 * 128) / 8);
                        __m128i b = _mm_load_si128(tmpPtr + 128 / 8);
                        __m128i c = _mm_load_si128(tmpPtr++);
                        __m128i sum = _mm_add_epi16(_mm_add_epi16(a, b), c);
                        __m128i avg = _mm_mulhi_epi16(sum, one_third);
                        _mm_store_si128(outPtr++, avg);
                    }
                }
            }
        }
#elif __ARM_NEON
            uint16x4_t one_third = vdup_n_u16(21846);
#pragma omp parallel for
            for (int yTile = 0; yTile < out.height(); yTile += 32) {
                uint16x8_t tmp[(128 / 8) * (32 + 2)];
                for (int xTile = 0; xTile < out.width(); xTile += 128) {
                    uint16_t *tmpPtr = (uint16_t *)tmp;
                    for (int y = 0; y < 32 + 2; y++) {
                        const uint16_t *inPtr = &(in(xTile, yTile + y));
                        for (int x = 0; x < 128; x += 8) {
                            uint16x8_t a = vld1q_u16(inPtr);
                            uint16x8_t b = vld1q_u16(inPtr + 1);
                            uint16x8_t c = vld1q_u16(inPtr + 2);
                            uint16x8_t sum = vaddq_u16(vaddq_u16(a, b), c);
                            uint16x4_t sumlo = vget_low_u16(sum);
                            uint16x4_t sumhi = vget_high_u16(sum);
                            uint16x4_t avglo = vshrn_n_u32(vmull_u16(sumlo, one_third), 16);
                            uint16x4_t avghi = vshrn_n_u32(vmull_u16(sumhi, one_third), 16);
                            uint16x8_t avg = vcombine_u16(avglo, avghi);
                            vst1q_u16(tmpPtr, avg);
                            tmpPtr += 8;
                            inPtr += 8;
                        }
                    }
                    tmpPtr = (uint16_t *)tmp;
                    for (int y = 0; y < 32; y++) {
                        uint16_t *outPtr = &(out(xTile, yTile + y));
                        for (int x = 0; x < 128; x += 8) {
                            uint16x8_t a = vld1q_u16(tmpPtr + (2 * 128));
                            uint16x8_t b = vld1q_u16(tmpPtr + 128);
                            uint16x8_t c = vld1q_u16(tmpPtr);
                            uint16x8_t sum = vaddq_u16(vaddq_u16(a, b), c);
                            uint16x4_t sumlo = vget_low_u16(sum);
                            uint16x4_t sumhi = vget_high_u16(sum);
                            uint16x4_t avglo = vshrn_n_u32(vmull_u16(sumlo, one_third), 16);
                            uint16x4_t avghi = vshrn_n_u32(vmull_u16(sumhi, one_third), 16);
                            uint16x8_t avg = vcombine_u16(avglo, avghi);
                            vst1q_u16(outPtr, avg);
                            tmpPtr += 8;
                            outPtr += 8;
                        }
                    }
                }
            }
#else
            // No intrinsics enabled, do a naive thing.
            for (int y = 0; y < out.height(); y++) {
                for (int x = 0; x < out.width(); x++) {
                    int tmp[3] = {
                        (in(x, y) + in(x + 1, y) + in(x + 2, y)) / 3,
                        (in(x, y + 1) + in(x + 1, y + 1) + in(x + 2, y + 1)) / 3,
                        (in(x, y + 2) + in(x + 1, y + 2) + in(x + 2, y + 2)) / 3,
                    };
                    out(x, y) = (tmp[0] + tmp[1] + tmp[2]) / 3;
                }
            }
#endif
    });

    return out;
}

#include "halide_blur.h"

Buffer<uint16_t, 2> blur_halide(Buffer<uint16_t, 2> in) {
    Buffer<uint16_t, 2> out(in.width() - 8, in.height() - 2);

    // Call it once to initialize the halide runtime stuff
    halide_blur(in, out);
    // Copy-out result if it's device buffer and dirty.
    out.copy_to_host();

    t = benchmark(10, 1, [&]() {
        // Compute the same region of the output as blur_fast (i.e., we're
        // still being sloppy with boundary conditions)
        halide_blur(in, out);
        // Sync device execution if any.
        out.device_sync();
    });

    out.copy_to_host();

    return out;
}

int main(int argc, char **argv) {
    const auto *md = halide_blur_metadata();
    const bool is_hexagon = strstr(md->target, "hvx_128") || strstr(md->target, "hvx_64");

    // The Hexagon simulator can't allocate as much memory as the above wants.
    const int width = is_hexagon ? 648 : 2568;
    const int height = is_hexagon ? 482 : 1922;

    Buffer<uint16_t, 2> input(width, height);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    Buffer<uint16_t, 2> blurry = blur(input);
    double slow_time = t;

    Buffer<uint16_t, 2> speedy = blur_fast(input);
    double fast_time = t;

    Buffer<uint16_t, 2> halide = blur_halide(input);
    double halide_time = t;

    printf("times: %f %f %f\n", slow_time, fast_time, halide_time);

    for (int y = 64; y < input.height() - 64; y++) {
        for (int x = 64; x < input.width() - 64; x++) {
            if (blurry(x, y) != speedy(x, y) || blurry(x, y) != halide(x, y)) {
                printf("difference at (%d,%d): %d %d %d\n", x, y, blurry(x, y), speedy(x, y), halide(x, y));
                abort();
            }
        }
    }

    printf("Success!\n");
    return 0;
}
