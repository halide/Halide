#include "Halide.h"
#include <cmath>
#include <cinttypes>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include <vector>

using namespace Halide;

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

float float_from_bits(uint32_t bits) {
    union {
        float asFloat;
        uint32_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asFloat;
}

uint32_t float_to_bits(float value) {
    union {
        float asFloat;
        uint32_t asUInt;
    } out;
    out.asFloat = value;
    return out.asUInt;
}

double double_from_bits(uint64_t bits) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asDouble;
}

uint64_t double_to_bits(double value) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asDouble = value;
    return out.asUInt;
}
struct Result {
    uint16_t RZ; // Result for round to Zero
    uint16_t RU; // Result for round to +ve infinity
    uint16_t RD; // Result for round to -ve infinity
    uint16_t RNE; // Result for round to nearest, ties to even
    uint16_t RNA; // Result for round to nearest, ties to away from zero
    static Result all(uint16_t v) {
        Result r = {.RZ = v, .RU = v, .RD = v, .RNE = v, .RNA = v};
        return r;
    }

    uint16_t get(RoundingMode rm) {
        switch (rm) {
            case RoundingMode::TowardZero:
                return RZ;
            case RoundingMode::ToNearestTiesToEven:
                return RNE;
            case RoundingMode::ToNearestTiesToAway:
                return RNA;
            case RoundingMode::TowardPositiveInfinity:
                return RU;
            case RoundingMode::TowardNegativeInfinity:
                return RD;
            default:
                h_assert(false, "Unsupported rounding mode");
        }
        h_assert(false, "Unreachable");
        // Silence GCC warning (-Wreturn-type)
        return 0;
    }
};

std::vector<std::pair<float, Result>> floatToFloat16Results;
std::vector<std::pair<double, Result>> doubleToFloat16Results;

void initExpectedResults() {
    /*
     * Exact rounding:
     *
     * These are constants that can be represented exactly in half
     */

    // +ve 0
    floatToFloat16Results.push_back(std::make_pair(0.0f, Result::all(0x0000)));
    //floatToFloat16Results.push_back(std::make_pair(0.0f, Result::all(0x0000)));
    doubleToFloat16Results.push_back(std::make_pair(0.0, Result::all(0x0000)));
    // -ve 0
    floatToFloat16Results.push_back(std::make_pair(-0.0f, Result::all(0x8000)));
    doubleToFloat16Results.push_back(std::make_pair(-0.0, Result::all(0x8000)));

    // +ve Infinity
    floatToFloat16Results.push_back(std::make_pair(INFINITY, Result::all(0x7c00)));
    doubleToFloat16Results.push_back(std::make_pair(INFINITY, Result::all(0x7c00)));

    // -ve Infinity
    floatToFloat16Results.push_back(std::make_pair(-INFINITY, Result::all(0xfc00)));
    doubleToFloat16Results.push_back(std::make_pair(-INFINITY, Result::all(0xfc00)));

    // Quiet naN
    floatToFloat16Results.push_back(std::make_pair(std::numeric_limits<float>::quiet_NaN(), Result::all(0x7e00)));
    doubleToFloat16Results.push_back(std::make_pair(std::numeric_limits<double>::quiet_NaN(), Result::all(0x7e00)));

    // +1.0
    floatToFloat16Results.push_back(std::make_pair(1.0f, Result::all(0x3c00)));
    doubleToFloat16Results.push_back(std::make_pair(1.0, Result::all(0x3c00)));

    // -1.0
    floatToFloat16Results.push_back(std::make_pair(-1.0f, Result::all(0xbc00)));
    doubleToFloat16Results.push_back(std::make_pair(-1.0, Result::all(0xbc00)));

    // 0x0.004p-14, the smallest +ve
    floatToFloat16Results.push_back(std::make_pair((1.0f)/(1<<24), Result::all(0x0001)));
    doubleToFloat16Results.push_back(std::make_pair((1.0)/(1<<24), Result::all(0x0001)));

    // 0x0.008p-14, the 2nd smallest +ve
    floatToFloat16Results.push_back(std::make_pair((1.0f)/(1<<23), Result::all(0x0002)));
    doubleToFloat16Results.push_back(std::make_pair((1.0)/(1<<23), Result::all(0x0002)));

    // -0x0.004p-14, the largest -ve
    floatToFloat16Results.push_back(std::make_pair((-1.0f)/(1<<24), Result::all(0x8001)));
    doubleToFloat16Results.push_back(std::make_pair((-1.0)/(1<<24), Result::all(0x8001)));

    // -0x0.008p-14, the 2nd smallest -ve
    floatToFloat16Results.push_back(std::make_pair((-1.0f)/(1<<23), Result::all(0x8002)));
    doubleToFloat16Results.push_back(std::make_pair((-1.0)/(1<<23), Result::all(0x8002)));

    // Largest +ve
    floatToFloat16Results.push_back(std::make_pair(65504.0f, Result::all(0x7bff)));
    doubleToFloat16Results.push_back(std::make_pair(65504.0, Result::all(0x7bff)));

    // smallest -ve
    floatToFloat16Results.push_back(std::make_pair(-65504.0f, Result::all(0xfbff)));
    doubleToFloat16Results.push_back(std::make_pair(-65504.0, Result::all(0xfbff)));

    // Largest float16 subnormal
    // 0x1.ff8000p-15
    floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x387fc000), Result::all(0x03ff)));
    // 0x1.ff8000000000p-15,
    doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3f0ff80000000000), Result::all(0x03ff)));

    // 0x1.a000p-16 this is represented as a normal as a float or double but
    // will become subnormal as a float16.
    floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x37d00000), Result::all(0x01a0)));
    doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3efa000000000000), Result::all(0x01a0)));

    // -0x1.a000p-16 this is represented as a normal as a float or double but
    // will become subnormal as a float16.
    floatToFloat16Results.push_back(std::make_pair(float_from_bits(0xb7d00000), Result::all(0x81a0)));
    doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0xbefa000000000000), Result::all(0x81a0)));

    /* Inexact rounding:
     * These values cannot be represented exactly in half
     */
    // Overflow: Not representable (2^16)
    {
        Result resultFromFloat = { .RZ = 0x7bff, // Never rounds to +inf
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff, // Never rounds to +inf
                                   .RNE = 0x7c00, // +inf
                                   .RNA = 0x7c00 // +inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(65536.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65536.0, resultFromFloat));
    }

    // Not representable (-2^16)
    {
        Result resultFromFloat = { .RZ = 0xfbff, // Never rounds to -inf
                                   .RU = 0xfbff, // Never rounds to -inf
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfc00, // -inf
                                   .RNA = 0xfc00 // -inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65536.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65536.0, resultFromFloat));
    }

    // 2^16 -1. This does not have an overflowing exponent but is still greater
    // than the largest representable binary16 (65504).
    {
        Result resultFromFloat = { .RZ = 0x7bff, // Never rounds to +inf
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff, // Never rounds to +inf
                                   .RNE = 0x7c00, // +inf
                                   .RNA = 0x7c00 // +inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(65535.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65535.0, resultFromFloat));
    }
    // -2^16 -1. This does not have an overflowing exponent but is still smaller
    // than the smallest representable binary16 (-65504).
    {
        Result resultFromFloat = { .RZ = 0xfbff, // Never rounds to -inf
                                   .RU = 0xfbff, // Never rounds to -inf
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfc00, // -inf
                                   .RNA = 0xfc00 // -inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65535.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65535.0, resultFromFloat));
    }

    // Underflow: This value is too small to be representable and will
    // be rounded to + zero apart from RU
    // 0x1.0p-26
    {
        Result resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0000,
                                   .RNA = 0x0000
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x32800000), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3e50000000000000), resultFromFloat));
    }
    // -0x1.0p-26
    // Will round to negative zero except for RD
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8000,
                                   .RNA = 0x8000
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0xb2800000), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0xbe50000000000000), resultFromFloat));
    }


    // ~0.1
    // Has infinitely repeating bit pattern
    // 0.000 1100 1100 1100 ...
    {
        // When rounding this constant the round bit is zero but the sticky bit is
        // one which means the values for the rounding modes will all be the same
        // accept for RU which will round up.
        Result resultFromFloat = { .RZ = 0x2e66,
                                   .RU = 0x2e67,
                                   .RD = 0x2e66,
                                   .RNE = 0x2e66,
                                   .RNA = 0x2e66
                                 };
        floatToFloat16Results.push_back(std::make_pair(0.1f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(0.1, resultFromFloat));
    }

    // -0.1
    {
        // When rounding this constant the round bit is zero but the sticky bit is
        // one. All rounding modes give the same answer apart from round down.
        Result resultFromFloat = { .RZ = 0xae66,
                                   .RU = 0xae66,
                                   .RD = 0xae67,
                                   .RNE = 0xae66,
                                   .RNA = 0xae66
                                 };
        floatToFloat16Results.push_back(std::make_pair(-0.1f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-0.1, resultFromFloat));
    }

    // 65488 is exactly half way between representable binary16
    // floats (65472 and 66504). When rounding the round bit will
    // be 1 but the stickbit will be zero.
    {
        Result resultFromFloat = { .RZ = 0x7bfe,
                                   .RU = 0x7bff,
                                   .RD = 0x7bfe,
                                   .RNE = 0x7bfe, // Tie, picks even significand
                                   .RNA = 0x7bff // Tie, picks value furthest from zero
                                 };
        floatToFloat16Results.push_back(std::make_pair(65488.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65488.0, resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0xfbfe,
                                   .RU = 0xfbfe,
                                   .RD = 0xfbff,
                                   .RNE = 0xfbfe, // Tie, picks even significand
                                   .RNA = 0xfbff // Tie, picks value furthest from zero
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65488.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65488.0, resultFromFloat));
    }
    // 65488.00390625 is slightly more than the half way point between the two
    // representable floats (65472 and 66504) so both the round bit and sticky
    // bit will be one.
    {
        Result resultFromFloat = { .RZ = 0x7bfe,
                                   .RU = 0x7bff,
                                   .RD = 0x7bfe,
                                   .RNE = 0x7bff,
                                   .RNA = 0x7bff
                                 };
        floatToFloat16Results.push_back(std::make_pair(65488.00390625f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65488.00390625, resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0xfbfe,
                                   .RU = 0xfbfe,
                                   .RD = 0xfbff,
                                   .RNE = 0xfbff,
                                   .RNA = 0xfbff
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65488.00390625f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65488.00390625, resultFromFloat));
    }

    // 65535.9, slightly smaller than 2^(e_max +1) so RZ and RD should not return
    // +inf
    {
        Result resultFromFloat = { .RZ = 0x7bff,
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff,
                                   .RNE = 0x7c00, // +inf
                                   .RNA = 0x7c00 // +inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(65535.9f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65535.9, resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0xfbff,
                                   .RU = 0xfbff,
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfc00, // -inf
                                   .RNA = 0xfc00 // -inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65535.9f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65535.9, resultFromFloat));
    }


    // 66520 is half way between the 66504 (representable in binary16)
    // and 66536 (not representable in binary16). For RNE and RNA
    // there is a tie and both must pick infinity
    {
        Result resultFromFloat = { .RZ = 0x7bff,
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff,
                                   .RNE = 0x7c00, // +inf
                                   .RNA = 0x7c00 // +inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(65520.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65520.0, resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0xfbff,
                                   .RU = 0xfbff,
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfc00, // -inf
                                   .RNA = 0xfc00 // -inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65520.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65520.0, resultFromFloat));
    }

    // 66519.9 is slightly less than 66520 so RNE and RNA should round toward
    // zero
    {
        Result resultFromFloat = { .RZ = 0x7bff,
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff,
                                   .RNE = 0x7bff,
                                   .RNA = 0x7bff
                                 };
        floatToFloat16Results.push_back(std::make_pair(65519.9f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65519.9, resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0xfbff,
                                   .RU = 0xfbff,
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfbff,
                                   .RNA = 0xfbff
                                 };
        floatToFloat16Results.push_back(std::make_pair(-65519.9f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(-65519.9, resultFromFloat));
    }

    // 1.0 * 2^-25. This is a tie for RNE and RNA.
    {
        Result resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0000, // pick even significand
                                   .RNA = 0x0001 // away from zero. Is this compliant??
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x33000000), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3e60000000000000), resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8000, // pick even significand
                                   .RNA = 0x8001 // away from zero. Is this compliant??
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0xb3000000), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0xbe60000000000000), resultFromFloat));
    }

    // 1.0 * 2^-25 + delta . Where delta is the smallest increment available for
    // the type. This is slightly over the tie boundary for RNE and RNA should
    // not round to zero.
    {
        Result resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0001,
                                   .RNA = 0x0001
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x33000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3e60000000000001), resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8001,
                                   .RNA = 0x8001
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0xb3000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0xbe60000000000001), resultFromFloat));
    }

    // Try to smallest subnormal number for the type.
    // Every rounding mode apart from RU should return 0
    {
        Result resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0000,
                                   .RNA = 0x0000
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x00000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x0000000000000001), resultFromFloat));
    }
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8000,
                                   .RNA = 0x8000
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x80000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x8000000000000001), resultFromFloat));
    }
}

std::pair<Image<float>,Image<float16_t>> getInputAndExpectedResultImagesF(unsigned width, unsigned height, RoundingMode rm) {
    Image<float> input(width, height);
    Image<float16_t> expected(width, height);

    int modValue = floatToFloat16Results.size();

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            auto resultPair = floatToFloat16Results[( x + y*width) % modValue ];
            input(x,y) = resultPair.first;
            expected(x,y) = float16_t::make_from_bits(resultPair.second.get(rm));
        }
    }
    return std::make_pair(input, expected);
}

std::pair<Image<double>,Image<float16_t>> getInputAndExpectedResultImagesD(unsigned width, unsigned height, RoundingMode rm) {
    Image<double> input(width, height);
    Image<float16_t> expected(width, height);

    int modValue = floatToFloat16Results.size();

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            auto resultPair = doubleToFloat16Results[( x + y*width) % modValue ];
            input(x,y) = resultPair.first;
            expected(x,y) = float16_t::make_from_bits(resultPair.second.get(rm));
        }
    }
    return std::make_pair(input, expected);
}

template <typename T>
void checkResults(Image<float16_t>& expected, Image<float16_t>& result) {
    h_assert(expected.width() == result.width(), "width mismatch");
    h_assert(expected.height() == result.height(), "height mismatch");

    // Check results
    for (unsigned x=0; x < result.width(); ++x) {
        for (unsigned y=0; y < result.height(); ++y) {
            float16_t expectedValue = expected(x, y);
            float16_t resultValue = result(x, y);
            // Need to convert to bits as NaN is not comparable
            uint16_t expectedValueAsBits = expectedValue.to_bits();
            uint16_t resultValueAsBits = resultValue.to_bits();
            if (resultValueAsBits != expectedValueAsBits) {
               printf("Failed to cast correctly: x:%u y:%u\n", x, y);
               printf("resultValueAsBits  : 0x%.4" PRIx16 "\n", resultValueAsBits);
               printf("expectedValueAsBits: 0x%.4" PRIx16 "\n", expectedValueAsBits);

              // Show input and possible outputs
              int index = (x + y* result.width()) % floatToFloat16Results.size();
              printf("data index: %u\n", index);
              Result r;
              if (sizeof(T) == sizeof(float)) {
                  std::pair<float,Result> resultPair = floatToFloat16Results[index];
                  r = resultPair.second;
                  printf("Input: 0x%.8" PRIx32 "(~%f)\n", float_to_bits(resultPair.first), resultPair.first);
              } else {
                  h_assert(sizeof(T) == sizeof(double), "wrong type?");
                  std::pair<double,Result> resultPair = doubleToFloat16Results[index];
                  r = resultPair.second;
                  printf("Input: 0x%.16" PRIx64 "(~%f)\n", double_to_bits(resultPair.first), resultPair.first);
              }
              printf("Expected result as RZ: 0x%.4" PRIx16 "\n", r.RZ);
              printf("Expected result as RU: 0x%.4" PRIx16 "\n", r.RU);
              printf("Expected result as RD: 0x%.4" PRIx16 "\n", r.RD);
              printf("Expected result as RNE: 0x%.4" PRIx16 "\n", r.RNE);
              printf("Expected result as RNA: 0x%.4" PRIx16 "\n", r.RNA);

              h_assert(false, "Failed conversion");
            }
        }
    }
}

void testFloatSingleRoundingMode(Target host,
                                 unsigned width,
                                 unsigned height,
                                 int vectorizeWidth,
                                 RoundingMode rm) {
    std::pair<Image<float>,Image<float16_t>> img = getInputAndExpectedResultImagesF(
        width,
        height,
        rm);

    Var x, y;
    Func downCast;
    downCast(x, y) = cast<float16_t>(img.first(x, y), rm);
    if (vectorizeWidth) {
        downCast.vectorize(x, vectorizeWidth);
    }
    Image<float16_t> result = downCast.realize( {img.first.width(), img.first.height()},
                                                host);
    // Check results
    checkResults<float>(img.second, result);
}


void testDoubleSingleRoundingMode(Target host,
                                 unsigned width,
                                 unsigned height,
                                 int vectorizeWidth,
                                 RoundingMode rm) {
    std::pair<Image<double>,Image<float16_t>> img = getInputAndExpectedResultImagesD(
        width,
        height,
        rm);

    Var x, y;
    Func downCast;
    downCast(x, y) = cast<float16_t>(img.first(x, y), rm);
    if (vectorizeWidth) {
        downCast.vectorize(x, vectorizeWidth);
    }
    Image<float16_t> result = downCast.realize( {img.first.width(), img.first.height()},
                                                host);
    // Check results
    checkResults<double>(img.second, result);
}

void testFloatAndDoubleConversion(Target host, Result modes, bool testDoubleConv, int vectorizeWidth=0) {
    int width = 10;
    int height = 10;
    if (vectorizeWidth > 0) {
        // use multiple of vectorization width
        width = 3*vectorizeWidth;
        height = 3*vectorizeWidth;
    }

    // Test float
    printf("Testing float -> float16\n");
    if (modes.RZ > 0) {
        printf("Testing RZ\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::TowardZero);
    } else {
        printf("Skipping RZ\n");
    }

    if (modes.RNE > 0) {
        printf("Testing RNE\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::ToNearestTiesToEven);
    } else {
        printf("Skipping RNE\n");
    }
    if (modes.RNA > 0) {
        printf("Testing RNA\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::ToNearestTiesToAway);
    } else {
        printf("Skipping RNA\n");
    }
    if (modes.RU > 0) {
        printf("Testing RU\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::TowardPositiveInfinity);
    } else {
        printf("Skipping RU\n");
    }
    if (modes.RD > 0) {
        printf("Testing RD\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::TowardNegativeInfinity);
    } else {
        printf("Skipping RD\n");
    }

    // Test double
    if (testDoubleConv) {
        printf("Testing double -> float16\n");
        if (modes.RZ > 0) {
            printf("Testing RZ\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::TowardZero);
        } else {
            printf("Skipping RZ\n");
        }

        if (modes.RNE > 0) {
            printf("Testing RNE\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::ToNearestTiesToEven);
        } else {
            printf("Skipping RNE\n");
        }
        if (modes.RNA > 0) {
            printf("Testing RNA\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::ToNearestTiesToAway);
        } else {
            printf("Skipping RNA\n");
        }
        if (modes.RU > 0) {
            printf("Testing RU\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::TowardPositiveInfinity);
        } else {
            printf("Skipping RU\n");
        }
        if (modes.RD > 0) {
            printf("Testing RD\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::TowardNegativeInfinity);
        } else {
            printf("Skipping RD\n");
        }
    } else {
        printf("Skipping double -> float16\n");
    }
}

int main(){
    initExpectedResults();
    h_assert(floatToFloat16Results.size() > 0, "floatToFloat16Results too small");
    h_assert(doubleToFloat16Results.size() > 0, "doubleToFloat16Results too small");
    h_assert(floatToFloat16Results.size() == doubleToFloat16Results.size(), "size mismatch");

    /*
     * Test software implementation of converting float16 to single
     * and double
     */
    Target host = get_jit_target_from_environment();

    // FIXME: This seems a bit cumbersome and fragile, perhaps we should have
    // a softf16c target feature that forces our software implementation to be used?

    // We want to test the software implementation of floating
    // point so remove support from target
    if (host.arch == Target::X86) {
        host.set_feature(Target::F16C, false);
    }
    // TODO: Add code for other architectures to disable their native float16
    // conversion support if they have it

    // Test all rounding modes
    Result roundingModesToTest = Result::all(0x0001);
    // FIXME: Need to fix the software implementation so we can re-enable this
    //testFloatAndDoubleConversion(host, roundingModesToTest, /*testDoubleConv=*/true);

    /*
     * Try to test hardware implementations of converting single and double to
     * float16
     */
    host = get_jit_target_from_environment();
    if (host.arch == Target::X86 && host.has_feature(Target::F16C)) {
        roundingModesToTest = {
            .RZ = 1,
            .RU = 1,
            .RD = 1,
            .RNE = 1,
            .RNA = 0, // Not supported by vcvtps2ph
        };
        printf("Trying no vectorization\n");
        testFloatAndDoubleConversion(host, /*FIXME: should be Result::all(0x0001)*/roundingModesToTest, /*testDoubleConv=*/ /*FIXME: should be true*/false, /*vectorizeWidth=*/0);

        printf("Trying vectorization width 4\n");
        // Note: No native support for "double -> float16" if vectorizing
        testFloatAndDoubleConversion(host, roundingModesToTest, /*testDoubleConv=*/ false, /*vectorizeWidth=*/4);

        printf("Trying vectorization width 8\n");
        // Note: No native support for "double -> float16" if vectorizing
        testFloatAndDoubleConversion(host, roundingModesToTest, /*testDoubleConv=*/ false, /*vectorizeWidth=*/8);

        printf("Trying vectorization width 10\n");
        // Note: No native support for "double -> float16" if vectorizing
        testFloatAndDoubleConversion(host, roundingModesToTest, /*testDoubleConv=*/ false, /*vectorizeWidth=*/10);
    }
    printf("Success!\n");
    return 0;
}
