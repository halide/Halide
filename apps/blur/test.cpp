#include <emmintrin.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "benchmark.h"
#include "halide_image.h"

using namespace Halide::Tools;

//#define cimg_display 0
//#include "CImg.h"
//using namespace cimg_library;

// typedef CImg<uint16_t> Image;

double t;


Image<uint16_t> blur(Image<uint16_t> in) {
    Image<uint16_t> tmp(in.width()-8, in.height());
    Image<uint16_t> out(in.width()-8, in.height()-2);

    t = benchmark(10, 1, [&]() {
        for (int y = 0; y < tmp.height(); y++)
            for (int x = 0; x < tmp.width(); x++)
                tmp(x, y) = (in(x, y) + in(x+1, y) + in(x+2, y))/3;

        for (int y = 0; y < out.height(); y++)
            for (int x = 0; x < out.width(); x++)
                out(x, y) = (tmp(x, y) + tmp(x, y+1) + tmp(x, y+2))/3;
    });

    return out;
}


Image<uint16_t> blur_fast(Image<uint16_t> in) {
    Image<uint16_t> out(in.width()-8, in.height()-2);

    t = benchmark(10, 1, [&]() {
        __m128i one_third = _mm_set1_epi16(21846);
#pragma omp parallel for
        for (int yTile = 0; yTile < out.height(); yTile += 32) {
            __m128i a, b, c, sum, avg;
            __m128i tmp[(128/8) * (32 + 2)];
            for (int xTile = 0; xTile < out.width(); xTile += 128) {
                __m128i *tmpPtr = tmp;
                for (int y = 0; y < 32+2; y++) {
                    const uint16_t *inPtr = &(in(xTile, yTile+y));
                    for (int x = 0; x < 128; x += 8) {
                        a = _mm_load_si128((__m128i*)(inPtr));
                        b = _mm_loadu_si128((__m128i*)(inPtr+1));
                        c = _mm_loadu_si128((__m128i*)(inPtr+2));
                        sum = _mm_add_epi16(_mm_add_epi16(a, b), c);
                        avg = _mm_mulhi_epi16(sum, one_third);
                        _mm_store_si128(tmpPtr++, avg);
                        inPtr+=8;
                    }
                }
                tmpPtr = tmp;
                for (int y = 0; y < 32; y++) {
                    __m128i *outPtr = (__m128i *)(&(out(xTile, yTile+y)));
                    for (int x = 0; x < 128; x += 8) {
                        a = _mm_load_si128(tmpPtr+(2*128)/8);
                        b = _mm_load_si128(tmpPtr+128/8);
                        c = _mm_load_si128(tmpPtr++);
                        sum = _mm_add_epi16(_mm_add_epi16(a, b), c);
                        avg = _mm_mulhi_epi16(sum, one_third);
                        _mm_store_si128(outPtr++, avg);
                    }
                }
            }
        }
    });

    return out;
}

/*
  Image blur_fast(const Image &in) {
  Image out(in.width(), in.height());

  __m128i one_third = _mm_set1_epi16(21846);
  #pragma omp parallel for
  for (int yTile = 0; yTile < in.height(); yTile += 64) {
  __m128i tmp[(64/8)*(64+2)];
  for (int xTile = 0; xTile < in.width(); xTile += 64) {
  __m128i *tmpPtr = tmp;
  for (int y = -1; y < 64+1; y++) {
  const uint16_t *inPtr = &(in(xTile, yTile+y));
  for (int x = 0; x < 64; x += 8) {
  __m128i val = _mm_loadu_si128((__m128i *)(inPtr-1));
  val = _mm_add_epi16(val, _mm_load_si128((__m128i *)inPtr));
  val = _mm_add_epi16(val, _mm_loadu_si128((__m128i *)(inPtr+1)));
  val = _mm_mulhi_epi16(val, one_third);
  _mm_store_si128(tmpPtr++, val);
  inPtr += 8;
  }
  }
  tmpPtr = tmp;
  for (int y = 0; y < 64; y++) {
  __m128i *outPtr = (__m128i *)(&(out(xTile, yTile+y)));
  for (int x = 0; x < 64; x += 8) {
  __m128i val = _mm_load_si128(tmpPtr);
  val = _mm_add_epi16(val, _mm_load_si128(tmpPtr+64/8));
  val = _mm_add_epi16(val, _mm_load_si128(tmpPtr+(2*64)/8));
  val = _mm_mulhi_epi16(val, one_third);
  _mm_store_si128(outPtr++, val);
  tmpPtr++;
  }
  }
  }
  }

  return out;
  }
*/


Image<uint16_t> blur_fast2(const Image<uint16_t> &in) {
    Image<uint16_t> out(in.width()-8, in.height()-2);

    int vw = in.width()/8;
    if (vw > 1024) {
        printf("Image too large for constant-sized stack allocation\n");
        return out;
    }

    t = benchmark(10, 1, [&]() {
        // multiplying by 21846 then taking the top 16 bits is equivalent to
        // dividing by three
        __m128i one_third = _mm_set1_epi16(21846);

#pragma omp parallel for
        for (int yTile = 0; yTile < in.height(); yTile += 128) {
            __m128i tmp[1024*4]; // four scanlines
            for (int y = -2; y < 128; y++) {
                // to produce this scanline of the output
                __m128i *outPtr = (__m128i *)(&(out(0, yTile + y)));
                // we use this scanline of the input
                const uint16_t *inPtr = &(in(0, yTile + y + 2));
                // and these scanlines of the intermediate result
                // We start y at negative 2 to fill the tmp buffer
                __m128i *tmpPtr0 = tmp + ((y+4) & 3) * vw;
                __m128i *tmpPtr1 = tmp + ((y+3) & 3) * vw;
                __m128i *tmpPtr2 = tmp + ((y+2) & 3) * vw;
                for (int x = 0; x < vw; x++) {
                    // blur horizontally to produce next scanline of tmp
                    __m128i val = _mm_load_si128((__m128i *)(inPtr));
                    val = _mm_add_epi16(val, _mm_loadu_si128((__m128i *)(inPtr+1)));
                    val = _mm_add_epi16(val, _mm_loadu_si128((__m128i *)(inPtr+2)));
                    val = _mm_mulhi_epi16(val, one_third);
                    _mm_store_si128(tmpPtr0++, val);

                    // blur vertically using previous scanlines of tmp to produce output
                    if (y >= 0) {
                        val = _mm_add_epi16(val, _mm_load_si128(tmpPtr1++));
                        val = _mm_add_epi16(val, _mm_load_si128(tmpPtr2++));
                        val = _mm_mulhi_epi16(val, one_third);
                        _mm_store_si128(outPtr++, val);
                    }

                    inPtr += 8;
                }
            }
        }

    });

    return out;
}

extern "C" {
#include "halide_blur.h"
}

Image<uint16_t> blur_halide(Image<uint16_t> in) {
    Image<uint16_t> out(in.width()-8, in.height()-2);

    // Call it once to initialize the halide runtime stuff
    halide_blur(in, out);

    t = benchmark(10, 1, [&]() {
        // Compute the same region of the output as blur_fast (i.e., we're
        // still being sloppy with boundary conditions)
        halide_blur(in, out);
    });

    return out;
}

int main(int argc, char **argv) {

    Image<uint16_t> input(6408, 4802);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    Image<uint16_t> blurry = blur(input);
    double slow_time = t;

    Image<uint16_t> speedy = blur_fast(input);
    double fast_time = t;

    //Image<uint16_t> speedy2 = blur_fast2(input);
    //float fast_time2 = t;

    Image<uint16_t> halide = blur_halide(input);
    double halide_time = t;

    // fast_time2 is always slower than fast_time, so skip printing it
    printf("times: %f %f %f\n", slow_time, fast_time, halide_time);

    for (int y = 64; y < input.height() - 64; y++) {
        for (int x = 64; x < input.width() - 64; x++) {
            if (blurry(x, y) != speedy(x, y) || blurry(x, y) != halide(x, y))
                printf("difference at (%d,%d): %d %d %d\n", x, y, blurry(x, y), speedy(x, y), halide(x, y));
        }
    }

    return 0;
}
