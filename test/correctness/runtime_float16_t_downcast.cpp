#include "HalideRuntime.h"
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include <vector>


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

double double_from_bits(uint64_t bits) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asDouble;
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
};

void test_float(float input, Result r) {
    uint16_t floatAsHalfRZ = halide_float_to_float16_bits(input,
                                                          halide_toward_zero);
    uint16_t floatAsHalfRU = halide_float_to_float16_bits(input,
                                                          halide_toward_positive_infinity);
    uint16_t floatAsHalfRD = halide_float_to_float16_bits(input,
                                                          halide_toward_negative_infinity);
    uint16_t floatAsHalfRNE = halide_float_to_float16_bits(input,
                                                           halide_to_nearest_ties_to_even);
    uint16_t floatAsHalfRNA = halide_float_to_float16_bits(input,
                                                           halide_to_nearest_ties_to_away);
    h_assert(floatAsHalfRZ == r.RZ, "Failed RZ round from float");
    h_assert(floatAsHalfRU == r.RU, "Failed RU round from float");
    h_assert(floatAsHalfRD == r.RD, "Failed RD round from float");
    h_assert(floatAsHalfRNE == r.RNE, "Failed RNE round from float");
    h_assert(floatAsHalfRNA == r.RNA, "Failed RNA round from float");
}

void test_double(double input, Result r) {
    uint16_t doubleAsHalfRZ = halide_double_to_float16_bits(input,
                                                            halide_toward_zero);
    uint16_t doubleAsHalfRU = halide_double_to_float16_bits(input,
                                                            halide_toward_positive_infinity);
    uint16_t doubleAsHalfRD = halide_double_to_float16_bits(input,
                                                            halide_toward_negative_infinity);
    uint16_t doubleAsHalfRNE = halide_double_to_float16_bits(input,
                                                             halide_to_nearest_ties_to_even);
    uint16_t doubleAsHalfRNA = halide_double_to_float16_bits(input,
                                                             halide_to_nearest_ties_to_away);
    h_assert(doubleAsHalfRZ == r.RZ, "Failed RZ round from double");
    h_assert(doubleAsHalfRU == r.RU, "Failed RU round from double");
    h_assert(doubleAsHalfRD == r.RD, "Failed RD round from double");
    h_assert(doubleAsHalfRNE == r.RNE, "Failed RNE round from double");
    h_assert(doubleAsHalfRNA == r.RNA, "Failed RNA round from double");
}

int main() {
    /*
     * Exact rounding:
     *
     * These are constants that can be represented exactly in half
     */

    // +ve 0
    test_float(0.0f, Result::all(0x0000));
    test_double(0.0, Result::all(0x0000));

    // -ve 0
    test_float(-0.0f, Result::all(0x8000));
    test_double(-0.0, Result::all(0x8000));

    // +ve Infinity
    test_float(INFINITY, Result::all(0x7c00));
    test_double(INFINITY, Result::all(0x7c00));

    // -ve Infinity
    test_float(-INFINITY, Result::all(0xfc00));
    test_double(-INFINITY, Result::all(0xfc00));

    // Quiet naN
    test_float(std::numeric_limits<float>::quiet_NaN(), Result::all(0x7e00));
    test_double(std::numeric_limits<double>::quiet_NaN(), Result::all(0x7e00));

    // +1.0
    test_float(1.0f, Result::all(0x3c00));
    test_double(1.0, Result::all(0x3c00));

    // -1.0
    test_float(-1.0f, Result::all(0xbc00));
    test_double(-1.0, Result::all(0xbc00));

    // 0x0.004p-14, the smallest +ve
    test_float((1.0f)/(1<<24), Result::all(0x0001));
    test_double((1.0)/(1<<24), Result::all(0x0001));

    // 0x0.008p-14, the 2nd smallest +ve
    test_float((1.0f)/(1<<23), Result::all(0x0002));
    test_double((1.0)/(1<<23), Result::all(0x0002));

    // -0x0.004p-14, the largest -ve
    test_float((-1.0f)/(1<<24), Result::all(0x8001));
    test_double((-1.0)/(1<<24), Result::all(0x8001));

    // -0x0.008p-14, the 2nd smallest -ve
    test_float((-1.0f)/(1<<23), Result::all(0x8002));
    test_double((-1.0)/(1<<23), Result::all(0x8002));

    // Largest +ve
    test_float(65504.0f, Result::all(0x7bff));
    test_double(65504.0, Result::all(0x7bff));

    // smallest -ve
    test_float(-65504.0f, Result::all(0xfbff));
    test_double(-65504.0, Result::all(0xfbff));

    // Largest float16 subnormal
    // 0x1.ff8000p-15
    test_float(float_from_bits(0x387fc000), Result::all(0x03ff));
    // 0x1.ff8000000000p-15,
    test_double(double_from_bits(0x3f0ff80000000000), Result::all(0x03ff));

    // 0x1.a000p-16 this is represented as a normal as a float or double but
    // will become subnormal as a float16.
    test_float(float_from_bits(0x37d00000), Result::all(0x01a0));
    test_double(double_from_bits(0x3efa000000000000), Result::all(0x01a0));

    // -0x1.a000p-16 this is represented as a normal as a float or double but
    // will become subnormal as a float16.
    test_float(float_from_bits(0xb7d00000), Result::all(0x81a0));
    test_double(double_from_bits(0xbefa000000000000), Result::all(0x81a0));

    /* Inexact rounding:
     * These values cannot be represented exactly in half
     */

    // Overflow: These values are not representable and will not round to
    // representable values
    // Should go to +infinity
    test_float(65536.0f, Result::all(0x7c00));
    test_double(65536.0, Result::all(0x7c00));

    // Should go to -infinity
    test_float(-65536.0f, Result::all(0xfc00));
    test_double(-65536.0, Result::all(0xfc00));

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
        test_float(float_from_bits(0x32800000), resultFromFloat);
        test_double(double_from_bits(0x3e50000000000000), resultFromFloat);
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
        test_float(float_from_bits(0xb2800000), resultFromFloat);
        test_double(double_from_bits(0xbe50000000000000), resultFromFloat);
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
        test_float(0.1f, resultFromFloat);
        test_double(0.1, resultFromFloat);
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
        test_float(-0.1f, resultFromFloat);
        test_double(-0.1, resultFromFloat);
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
        test_float(65488.0f, resultFromFloat);
        test_double(65488.0, resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0xfbfe,
                                   .RU = 0xfbfe,
                                   .RD = 0xfbff,
                                   .RNE = 0xfbfe, // Tie, picks even significand
                                   .RNA = 0xfbff // Tie, picks value furthest from zero
                                 };
        test_float(-65488.0f, resultFromFloat);
        test_double(-65488.0, resultFromFloat);
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
        test_float(65488.00390625f, resultFromFloat);
        test_double(65488.00390625, resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0xfbfe,
                                   .RU = 0xfbfe,
                                   .RD = 0xfbff,
                                   .RNE = 0xfbff,
                                   .RNA = 0xfbff
                                 };
        test_float(-65488.00390625f, resultFromFloat);
        test_double(-65488.00390625, resultFromFloat);
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
        test_float(65535.9f, resultFromFloat);
        test_double(65535.9, resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0xfbff,
                                   .RU = 0xfbff,
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfc00, // -inf
                                   .RNA = 0xfc00 // -inf
                                 };
        test_float(-65535.9f, resultFromFloat);
        test_double(-65535.9, resultFromFloat);
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
        test_float(65520.0f, resultFromFloat);
        test_double(65520.0, resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0xfbff,
                                   .RU = 0xfbff,
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfc00, // -inf
                                   .RNA = 0xfc00 // -inf
                                 };
        test_float(-65520.0f, resultFromFloat);
        test_double(-65520.0, resultFromFloat);
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
        test_float(65519.9f, resultFromFloat);
        test_double(65519.9, resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0xfbff,
                                   .RU = 0xfbff,
                                   .RD = 0xfc00, // -inf
                                   .RNE = 0xfbff,
                                   .RNA = 0xfbff
                                 };
        test_float(-65519.9f, resultFromFloat);
        test_double(-65519.9, resultFromFloat);
    }

    // 1.0 * 2^-25. This is a tie for RNE and RNA.
    {
        Result resultFromFloat = { .RZ = 0x0000,
                                   .RU = 0x0001,
                                   .RD = 0x0000,
                                   .RNE = 0x0000, // pick even significand
                                   .RNA = 0x0001 // away from zero. Is this compliant??
                                 };
        test_float(float_from_bits(0x33000000), resultFromFloat);
        test_double(double_from_bits(0x3e60000000000000), resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8000, // pick even significand
                                   .RNA = 0x8001 // away from zero. Is this compliant??
                                 };
        test_float(float_from_bits(0xb3000000), resultFromFloat);
        test_double(double_from_bits(0xbe60000000000000), resultFromFloat);
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
        test_float(float_from_bits(0x33000001), resultFromFloat);
        test_double(double_from_bits(0x3e60000000000001), resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8001,
                                   .RNA = 0x8001
                                 };
        test_float(float_from_bits(0xb3000001), resultFromFloat);
        test_double(double_from_bits(0xbe60000000000001), resultFromFloat);
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
        test_float(float_from_bits(0x00000001), resultFromFloat);
        test_double(double_from_bits(0x0000000000000001), resultFromFloat);
    }
    {
        Result resultFromFloat = { .RZ = 0x8000,
                                   .RU = 0x8000,
                                   .RD = 0x8001,
                                   .RNE = 0x8000,
                                   .RNA = 0x8000
                                 };
        test_float(float_from_bits(0x80000001), resultFromFloat);
        test_double(double_from_bits(0x8000000000000001), resultFromFloat);
    }
    printf("Success!\n");
    return 0;
}
