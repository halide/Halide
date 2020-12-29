#include "HalideRuntime.h"

extern "C" {

// Will this work on big-endian?
WEAK float halide_float16_bits_to_float(uint16_t bits) {
    uint32_t signMask = (bits & 0x8000) << 16;
    union {
        float asFloat;
        uint32_t asUInt;
    } result;

    uint32_t significandBits = (bits & 0x3ff);

    // Half has exponent stored as bias-15
    int exponent = ((bits & 0x7c00) >> 10);
    exponent -= 15;

    if (exponent == -15 && significandBits != 0) {
        // Subnormal number
        // Every subnormal in binary16 is represented as
        // a normal number in binary32 so we need to convert
        // to a normalised form.

        // Find index (right to left starting at zero)  of the most significant
        // bit in significand.
        uint32_t newSignificand = significandBits;
        // Is it safe to use this built-in?
        int leadingZeros = __builtin_clz(significandBits);
        int setMSB = 31 - leadingZeros;
        // Zero the leading bit which isn't represented in the IEEE-754 2008 binary32 normalised format
        newSignificand &= ~(1 << setMSB);
        // Now move bits into the correct position
        newSignificand <<= (23 - setMSB);
        // Compute the new exponent for the normalised form
        int newExponent = -24 + setMSB;  // (-14 - (10 - setMSB))
        uint32_t reEncodedExponent = newExponent + 127;
        result.asUInt = signMask | (reEncodedExponent << 23) | newSignificand;

    } else {
        // Normalised number, NaN, zero or infinity.
        // Here we can just zero extended the significand
        // and re-encode the exponent as appropriate
        //
        // In binary16 the stored significand is 10 bits and
        // in binary32 the stored significant is 23 bits so
        // we need to shift left by 13 bits.
        significandBits <<= 13;

        uint32_t reEncodedExponent;
        if (exponent == -15) {
            // Zero
            reEncodedExponent = 0x00;
        } else if (exponent == 16) {
            // NaN or Infinity
            reEncodedExponent = 0xff;
        } else {
            // Normal number (exponent stored as bias 127)
            reEncodedExponent = exponent + 127;
        }
        result.asUInt = signMask | (reEncodedExponent << 23) | significandBits;
    }
    return result.asFloat;
}

WEAK double halide_float16_bits_to_double(uint16_t bits) {
    // Just use native support for converting between float
    // and double
    float valueAsFloat = halide_float16_bits_to_float(bits);
    return (double)valueAsFloat;
}
}
