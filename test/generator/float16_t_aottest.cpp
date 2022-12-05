#include "HalideRuntime.h"
#include <cmath>
#include <limits>
#include <stdio.h>
#include <stdlib.h>

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

int main() {
    uint16_t inputs[] = {
        0x0000,  // +ve zero
        0x8000,  // -ve zero
        0x7c00,  // +ve infinity
        0xfc00,  // -ve infinity
        0x7e00,  // quiet NaN
        0x7bff,  // Largest +ve normal number
        0xfbff,  // Smallest -ve normal number
        0x0001,  // Smallest +ve subnormal number
        0x8001,  // Largest -ve subnormal number
        0x0002,  // 2nd smallest +ve subnormal number
        0x8002,  // 2nd largest -ve subnormal number
        0x0003,  // 3rd smallest subnormal number
        0x03ff,  // Largest subnormal
        0x03fe,  // 2nd largest subnormal
        0x3c00,  // 1.0
        0xbc00   // -1.0
    };

    // Unfortunately we can't use the C99 hex float format
    // here because MSVC doesn't support them
    float expectedF[] = {
        0.0f,
        -0.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        65504.0f,
        -65504.0f,
        (1.0f) / (1 << 24),
        (-1.0f) / (1 << 24),
        (1.0f) / (1 << 23),           // 0x1.000000p-23
        (-1.0f) / (1 << 23),          // -0x1.000000p-23
        (1.5f) / (1 << 23),           // 0x1.800000p-23
        float_from_bits(0x387fc000),  // 0x1.ff8000p-15,
        float_from_bits(0x387f8000),  // 0x1.ff0000p-15,
        1.0f,
        -1.0f};

    double expectedD[] = {
        0.0,
        -0.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        65504.0,
        -65504.0,
        (1.0) / (1 << 24),
        (-1.0) / (1 << 24),
        (1.0) / (1 << 23),                     // 0x1.000000000000p-23
        (-1.0) / (1 << 23),                    // -0x1.000000000000p-23
        (1.5) / (1 << 23),                     // 0x1.800000000000p-23
        double_from_bits(0x3f0ff80000000000),  // 0x1.ff8000000000p-15,
        double_from_bits(0x3f0ff00000000000),  // 0x1.ff0000000000p-15,
        1.0,
        -1.0};

    h_assert(sizeof(inputs) / sizeof(uint16_t) == sizeof(expectedF) / sizeof(float),
             "size of half array doesn't match float array");
    h_assert(sizeof(inputs) / sizeof(uint16_t) == sizeof(expectedD) / sizeof(double),
             "size of half array doesn't match double array");

    for (unsigned int index = 0; index < sizeof(inputs) / sizeof(uint16_t); ++index) {
        uint16_t in = inputs[index];
        union {
            float asFloat;
            uint32_t asUInt;
        } outF;
        outF.asFloat = halide_float16_bits_to_float(in);
        union {
            double asDouble;
            uint64_t asUInt;
        } outD;
        outD.asDouble = halide_float16_bits_to_double(in);

        union {
            float asFloat;
            uint32_t asUInt;
        } expectedFValue;
        expectedFValue.asFloat = expectedF[index];

        union {
            double asDouble;
            uint64_t asUInt;
        } expectedDValue;
        expectedDValue.asDouble = expectedD[index];

        // Compare bits because NaN in not comparable
        h_assert(outF.asUInt == expectedFValue.asUInt, "Failed to match on convert to float");
        h_assert(outD.asUInt == expectedDValue.asUInt, "Failed to match on convert to double");
    }
    printf("Success!\n");
    return 0;
}
