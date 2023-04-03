#include "Halide.h"
#include <cmath>
#include <stdio.h>

using namespace Halide;

void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    // Special constants

    // positive Zero
    {
        printf("Checking positive zero...\n");
        // Try constructing positive zero in different ways and check they all represent
        // the same float16_t
        const float16_t zeroDefaultConstructor;
        const float16_t zeroP = float16_t::make_zero();
        const float16_t zeroPFromFloat(0.0f);
        const float16_t zeroPFromDouble(0.0);
        const float16_t zeroPFromInt(0);
        h_assert(zeroDefaultConstructor.to_bits() == zeroP.to_bits(), "Mismatch between constructors");
        h_assert(zeroPFromFloat.to_bits() == zeroP.to_bits(), "Mistmatch between constructors");
        h_assert(zeroPFromDouble.to_bits() == zeroP.to_bits(), "Mistmatch between constructors");
        h_assert(zeroPFromInt.to_bits() == zeroP.to_bits(), "make_from_signed_int gave wrong value");

        // Check the representation
        h_assert(zeroP.is_zero() && !zeroP.is_negative(), "positive zero invalid");
        h_assert(zeroP.to_bits() == 0x0000, "positive zero invalid bits");

        // Try converting to native float types
        h_assert(((float)zeroP) == 0.0f, "positive zero conversion to float invalid");
        h_assert(((double)zeroP) == 0.0, "positive zero conversion to double invalid");
    }

    // negative Zero
    {
        printf("Checking negative zero...\n");
        // Try constructing negative zero in different ways and check they all represent
        // the same float16_t
        const float16_t zeroN = float16_t::make_negative_zero();
        const float16_t zeroNFromFloat(-0.0f);
        const float16_t zeroNFromDouble(-0.0);
        h_assert(zeroNFromFloat.to_bits() == zeroN.to_bits(), "Mismatch between constructors");
        h_assert(zeroNFromDouble.to_bits() == zeroN.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(zeroN.to_bits() == 0x8000, "negative zero invalid bits");
        h_assert(zeroN.is_zero(), "negative zero is not zero");
        h_assert(zeroN.is_negative(), "negative zero is not negative");

        // Try converting to native float types
        h_assert(((float)zeroN) == -0.0f, "negative zero conversion to float invalid");
        h_assert(((double)zeroN) == -0.0, "negative zero conversion to double invalid");
    }

    // positive infinity
    {
        printf("Checking positive infinity...\n");
        // Try constructing positive infinity in different ways and check they all
        // represent the same float16_t
        const float16_t infinityP = float16_t::make_infinity();
        const float16_t infinityPFromFloat(std::numeric_limits<float>::infinity());
        const float16_t infinityPFromDouble(std::numeric_limits<double>::infinity());
        h_assert(infinityPFromFloat.to_bits() == infinityP.to_bits(), "Mismatch between constructors");
        h_assert(infinityPFromDouble.to_bits() == infinityP.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(infinityP.is_infinity() && !infinityP.is_negative(), "positive infinity invalid");
        h_assert(infinityP.to_bits() == 0x7c00, "positive infinity invalid bits");

        // Try converting to native float types
        float infinityPf = (float)infinityP;
        double infinityPd = (double)infinityP;
        h_assert(std::isinf(infinityPf) & !std::signbit(infinityPf),
                 "positive infinity conversion to float invalid");
        h_assert(std::isinf(infinityPd) & !std::signbit(infinityPd),
                 "positive infinity conversion to double invalid");
    }

    // negative infinity
    {
        printf("Checking negative infinity...\n");
        // Try constructing negative infinity in different ways and check they all
        // represent the same float16_t
        const float16_t infinityN = float16_t::make_negative_infinity();
        const float16_t infinityNFromFloat(-std::numeric_limits<float>::infinity());
        const float16_t infinityNFromDouble(-std::numeric_limits<double>::infinity());
        h_assert(infinityNFromFloat.to_bits() == infinityN.to_bits(), "Mismatch between constructors");
        h_assert(infinityNFromDouble.to_bits() == infinityN.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(infinityN.is_infinity() && infinityN.is_negative(), "negative infinity invalid");
        h_assert(infinityN.to_bits() == 0xfc00, "negative infinity invalid bits");

        // Try converting to native float types
        float infinityNf = (float)infinityN;
        double infinityNd = (double)infinityN;
        h_assert(std::isinf(infinityNf) & std::signbit(infinityNf),
                 "negative infinity conversion to float invalid");
        h_assert(std::isinf(infinityNd) & std::signbit(infinityNd),
                 "negative infinity conversion to double invalid");
    }

    // NaN
    {
        printf("Checking NaN...\n");
        // Try constructing NaN in different ways and check they all
        // represent the same float16_t
        const float16_t nanValue = float16_t::make_nan();
        const float16_t nanValueFromFloat(std::numeric_limits<float>::quiet_NaN());
        const float16_t nanValueFromDouble(std::numeric_limits<double>::quiet_NaN());
        h_assert(nanValueFromFloat.to_bits() == nanValue.to_bits(), "Mismatch between constructors");
        h_assert(nanValueFromDouble.to_bits() == nanValue.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(nanValue.is_nan(), "NaN invalid");
        // Check exponent is all ones
        h_assert((nanValue.to_bits() & 0x7c00) == 0x7c00, "NaN exponent invalid");
        // Check significand is non zero
        h_assert((nanValue.to_bits() & 0x03ff) > 0, "NaN significant invalid");

        // Try converting to native float types
        float nanValuef = (float)nanValue;
        double nanValued = (double)nanValue;
        h_assert(std::isnan(nanValuef), "NaN conversion to float invalid");
        h_assert(std::isnan(nanValued), "NaN conversion to double invalid");
    }

    // Test the rounding of a few constants
    struct test_case {
        double val;
        uint16_t bits;
    };

    test_case tests[] = {
        {1.0 / (1 << 24), 0x001},    // smallest positive
        {-1.0 / (1 << 24), 0x8001},  // smallest negative
        {65504, 0x7bff},             // largest positive
        {-65504, 0xfbff},            // largest negative
        {0.1, 0x2e66},
        {0.3, 0x34cd},
        {4091, 0x6bfe},
        {-4091, 0xebfe},
        {1000000, 0x7c00},   // Out of range maps to +infinity
        {-1000000, 0xfc00},  // Out of range maps to -infinity
    };

    for (auto test : tests) {
        const float16_t v(test.val);
        if (v.to_bits() != test.bits) {
            printf("Rounding error: %f -> %u instead of %u\n", test.val, v.to_bits(), test.bits);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
