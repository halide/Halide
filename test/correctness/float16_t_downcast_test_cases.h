#ifndef FLOAT16_T_DOWNCAST_TEST_CASES_H
#define FLOAT16_T_DOWNCAST_TEST_CASES_H
#include <cinttypes>
#include <cmath>
#include <vector>
#include <utility>

struct DownCastedValue {
    uint16_t RZ; // Result for round to Zero
    uint16_t RU; // Result for round to +ve infinity
    uint16_t RD; // Result for round to -ve infinity
    uint16_t RNE; // Result for round to nearest, ties to even
    uint16_t RNA; // Result for round to nearest, ties to away from zero
    static DownCastedValue all(uint16_t v) {
        DownCastedValue r = {.RZ = v, .RU = v, .RD = v, .RNE = v, .RNA = v};
        return r;
    }

};

typedef std::vector<std::pair<float, DownCastedValue>> Float16ToFloatMap;
typedef std::vector<std::pair<double, DownCastedValue>> Float16ToDoubleMap;
std::pair<Float16ToFloatMap,Float16ToDoubleMap> get_float16_t_downcast_test_cases() {
    Float16ToFloatMap floatToFloat16Results;
    Float16ToDoubleMap doubleToFloat16Results;
    /*
     * Exact rounding:
     *
     * These are constants that can be represented exactly in half
     */

    // +ve 0
    floatToFloat16Results.push_back(std::make_pair(0.0f, DownCastedValue::all(0x0000)));
    doubleToFloat16Results.push_back(std::make_pair(0.0, DownCastedValue::all(0x0000)));
    // -ve 0
    floatToFloat16Results.push_back(std::make_pair(-0.0f, DownCastedValue::all(0x8000)));
    doubleToFloat16Results.push_back(std::make_pair(-0.0, DownCastedValue::all(0x8000)));

    // +ve Infinity
    floatToFloat16Results.push_back(std::make_pair(INFINITY, DownCastedValue::all(0x7c00)));
    doubleToFloat16Results.push_back(std::make_pair(INFINITY, DownCastedValue::all(0x7c00)));

    // -ve Infinity
    floatToFloat16Results.push_back(std::make_pair(-INFINITY, DownCastedValue::all(0xfc00)));
    doubleToFloat16Results.push_back(std::make_pair(-INFINITY, DownCastedValue::all(0xfc00)));

    // Quiet naN
    floatToFloat16Results.push_back(std::make_pair(std::numeric_limits<float>::quiet_NaN(), DownCastedValue::all(0x7e00)));
    doubleToFloat16Results.push_back(std::make_pair(std::numeric_limits<double>::quiet_NaN(), DownCastedValue::all(0x7e00)));

    // +1.0
    floatToFloat16Results.push_back(std::make_pair(1.0f, DownCastedValue::all(0x3c00)));
    doubleToFloat16Results.push_back(std::make_pair(1.0, DownCastedValue::all(0x3c00)));

    // -1.0
    floatToFloat16Results.push_back(std::make_pair(-1.0f, DownCastedValue::all(0xbc00)));
    doubleToFloat16Results.push_back(std::make_pair(-1.0, DownCastedValue::all(0xbc00)));

    // 0x0.004p-14, the smallest +ve
    floatToFloat16Results.push_back(std::make_pair((1.0f)/(1<<24), DownCastedValue::all(0x0001)));
    doubleToFloat16Results.push_back(std::make_pair((1.0)/(1<<24), DownCastedValue::all(0x0001)));

    // 0x0.008p-14, the 2nd smallest +ve
    floatToFloat16Results.push_back(std::make_pair((1.0f)/(1<<23), DownCastedValue::all(0x0002)));
    doubleToFloat16Results.push_back(std::make_pair((1.0)/(1<<23), DownCastedValue::all(0x0002)));

    // -0x0.004p-14, the largest -ve
    floatToFloat16Results.push_back(std::make_pair((-1.0f)/(1<<24), DownCastedValue::all(0x8001)));
    doubleToFloat16Results.push_back(std::make_pair((-1.0)/(1<<24), DownCastedValue::all(0x8001)));

    // -0x0.008p-14, the 2nd smallest -ve
    floatToFloat16Results.push_back(std::make_pair((-1.0f)/(1<<23), DownCastedValue::all(0x8002)));
    doubleToFloat16Results.push_back(std::make_pair((-1.0)/(1<<23), DownCastedValue::all(0x8002)));

    // Largest +ve
    floatToFloat16Results.push_back(std::make_pair(65504.0f, DownCastedValue::all(0x7bff)));
    doubleToFloat16Results.push_back(std::make_pair(65504.0, DownCastedValue::all(0x7bff)));

    // smallest -ve
    floatToFloat16Results.push_back(std::make_pair(-65504.0f, DownCastedValue::all(0xfbff)));
    doubleToFloat16Results.push_back(std::make_pair(-65504.0, DownCastedValue::all(0xfbff)));

    // Largest float16 subnormal
    // 0x1.ff8000p-15
    floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x387fc000), DownCastedValue::all(0x03ff)));
    // 0x1.ff8000000000p-15,
    doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3f0ff80000000000), DownCastedValue::all(0x03ff)));

    // 0x1.a000p-16 this is represented as a normal as a float or double but
    // will become subnormal as a float16.
    floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x37d00000), DownCastedValue::all(0x01a0)));
    doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3efa000000000000), DownCastedValue::all(0x01a0)));

    // -0x1.a000p-16 this is represented as a normal as a float or double but
    // will become subnormal as a float16.
    floatToFloat16Results.push_back(std::make_pair(float_from_bits(0xb7d00000), DownCastedValue::all(0x81a0)));
    doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0xbefa000000000000), DownCastedValue::all(0x81a0)));

    /* Inexact rounding:
     * These values cannot be represented exactly in half
     */
    // Overflow: Not representable (2^16)
    {
        DownCastedValue resultFromFloat = { .RZ = 0x7bff, // Never rounds to +inf
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
        DownCastedValue resultFromFloat = { .RZ = 0xfbff, // Never rounds to -inf
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
        DownCastedValue resultFromFloat = { .RZ = 0x7bff, // Never rounds to +inf
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
        DownCastedValue resultFromFloat = { .RZ = 0xfbff, // Never rounds to -inf
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
        DownCastedValue resultFromFloat = { .RZ = 0x0000,
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
        DownCastedValue resultFromFloat = { .RZ = 0x8000,
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
        DownCastedValue resultFromFloat = { .RZ = 0x2e66,
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
        DownCastedValue resultFromFloat = { .RZ = 0xae66,
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
        DownCastedValue resultFromFloat = { .RZ = 0x7bfe,
                                   .RU = 0x7bff,
                                   .RD = 0x7bfe,
                                   .RNE = 0x7bfe, // Tie, picks even significand
                                   .RNA = 0x7bff // Tie, picks value furthest from zero
                                 };
        floatToFloat16Results.push_back(std::make_pair(65488.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65488.0, resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0xfbfe,
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
        DownCastedValue resultFromFloat = { .RZ = 0x7bfe,
                                   .RU = 0x7bff,
                                   .RD = 0x7bfe,
                                   .RNE = 0x7bff,
                                   .RNA = 0x7bff
                                 };
        floatToFloat16Results.push_back(std::make_pair(65488.00390625f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65488.00390625, resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0xfbfe,
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
        DownCastedValue resultFromFloat = { .RZ = 0x7bff,
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff,
                                   .RNE = 0x7c00, // +inf
                                   .RNA = 0x7c00 // +inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(65535.9f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65535.9, resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0xfbff,
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
        DownCastedValue resultFromFloat = { .RZ = 0x7bff,
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff,
                                   .RNE = 0x7c00, // +inf
                                   .RNA = 0x7c00 // +inf
                                 };
        floatToFloat16Results.push_back(std::make_pair(65520.0f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65520.0, resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0xfbff,
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
        DownCastedValue resultFromFloat = { .RZ = 0x7bff,
                                   .RU = 0x7c00, // +inf
                                   .RD = 0x7bff,
                                   .RNE = 0x7bff,
                                   .RNA = 0x7bff
                                 };
        floatToFloat16Results.push_back(std::make_pair(65519.9f, resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(65519.9, resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0xfbff,
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
        DownCastedValue resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0000, // pick even significand
                                   .RNA = 0x0001 // away from zero. Is this compliant??
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x33000000), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3e60000000000000), resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0x8000,
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
        DownCastedValue resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0001,
                                   .RNA = 0x0001
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x33000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x3e60000000000001), resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0x8000,
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
        DownCastedValue resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0000,
                                   .RNA = 0x0000
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x00000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x0000000000000001), resultFromFloat));
    }
    {
        DownCastedValue resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8000,
                                   .RNA = 0x8000
                                 };
        floatToFloat16Results.push_back(std::make_pair(float_from_bits(0x80000001), resultFromFloat));
        doubleToFloat16Results.push_back(std::make_pair(double_from_bits(0x8000000000000001), resultFromFloat));
    }

    if (floatToFloat16Results.size() != doubleToFloat16Results.size()) {
        abort();
    }
    return std::make_pair(floatToFloat16Results, doubleToFloat16Results);
}
#endif
