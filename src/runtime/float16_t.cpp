#include "runtime_internal.h"

extern "C" {

// Note this function assumes little endian
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Only little endian supported
#endif
WEAK float halide_float16_t_to_float(struct halide_float16_t value) {
    // FIXME: Could really do with static_asserts here!
    //static_assert(sizeof(struct halide_float16_t) == 2,"");
    //static_assert(sizeof(float) == 4, "");
    uint16_t bits = value.data;
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
       int leadingZeros =  __builtin_clz(significandBits);
       // Chop off the leading bit which isn't stored
       // in the normalised number
       newSignificand <<= (leadingZeros + 1);
       // Now move bits into the correct position
       newSignificand >>= 9;
       // Compute the new exponent for the normalised form
       int setMSB = 31 - leadingZeros;
       int newExponent = exponent - (setMSB + 1);
       uint32_t reEncodedExponent = newExponent + 127;
       result.asUInt = signMask | (reEncodedExponent << 23) | newSignificand;

    } else {
        // Normalised number, NaN, zero or infinity.
        // Here we can just zero extended the significand
        // and re-encode the exponent.
        //
        // In binary16 the stored significand is 10 bits and
        // in binary32 the stored significant is 23 bits so
        // we need to shift left by 13 bits.
        significandBits <<= 13;

        // binary32 exponent is stored as bias 127
        uint32_t reEncodedExponent = exponent + 127;
        result.asUInt = signMask | (reEncodedExponent << 23) | significandBits;
    }
    return result.asFloat;
}

WEAK double halide_float16_t_to_double(struct halide_float16_t value) {
    // Just use native support for converting between float
    // and double
    float valueAsFloat = halide_float16_t_to_float(value);
    return (double) valueAsFloat;
}

}
