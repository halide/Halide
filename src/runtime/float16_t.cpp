#include "runtime_internal.h"
#include "HalideRuntime.h"

extern "C" {

// Will this work on big-endian?
WEAK float halide_float16_bits_to_float(uint16_t bits) {
    // FIXME: Could really do with static_asserts here!
    //static_assert(sizeof(float) == 4, "");
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
       int setMSB = 31 - leadingZeros;
       // Zero the leading bit which isn't represented in the IEEE-754 2008 binary32 normalised format
       newSignificand &= ~(1 << setMSB);
       // Now move bits into the correct position
       newSignificand <<= (23 - setMSB);
       // Compute the new exponent for the normalised form
       int newExponent = -24 + setMSB; // (-14 - (10 - setMSB))
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
    return (double) valueAsFloat;
}

/* The implementation here is based on the description of
 * rounding from "The Handbook of Floating-Point Arithmetic" in
 * 2.2 (Rounding) and 8.2 (Implementing IEEE-754 2008 Rounding).
 */
__attribute__((always_inline)) static uint16_t performRounding(uint16_t result,
                                                               bool roundBit,
                                                               bool stickyBit,
                                                               uint16_t signMask) {
    // Computation of the successor in IEEE754 binary16 is very elegant. Simply
    // adding 1 will compute the successor and will handle incrementing the
    // exponent correctly. It should move into +/- Infinity correctly too.
    uint16_t successor = result + 1;

    /* Rounding: We now need to pick between ``result`` which is the input rounded
     * down (or the exact result) and the ``successor`` (rounded up) based on
     * the rounding mode (round to nearest, ties to even).
     */
    if (!roundBit) {
        // round down
        return result;
    } else {
        if (stickyBit) {
            // round up
            return successor;
        } else {
            // There is a tie
            // Pick the result with the even significand
            if ((successor & 0x0001) == 0) {
                return successor;
            } else {
                return result;
            }
        }
    }
}

WEAK uint16_t halide_float_to_float16_bits(float value) {
   union {
        float asFloat;
        uint32_t asUInt;
    } data;
    // FIXME: Need C++11 to use this
    //static_assert(sizeof(float) == sizeof(uint32_t), "float is wrong size");
    data.asFloat = value;
    uint32_t bits = data.asUInt;
    uint32_t signMask = (bits & 0x80000000) >> (31 - 15);
    int32_t exponent = (bits & 0x7f800000) >> 23; // Shift into position
    exponent -= 127; // binary32 exponent is stored as bias-127
    uint32_t significandBits = (bits & 0x007fffff);

    // e_max + 1 for binary32
    if (exponent == 128 ) {
        if (significandBits == 0) {
            // +ve/-ve infinity
            return signMask | (0x1f << 10);
        } else {
            // NaN
            // Should we be dropping the sign?
            return 0x7e00;
        }
    }

    uint32_t truncatedSignificand=0;
    bool stickyBit = false;
    if (exponent <= -15) {
        // The floating point number is subnormal
        // as binary16 or zero
        //
        // Convert into subnormal form for binary 16
        // e.g.
        // 1.1 *2^-16 ==> 0.011* 2^-14
        int32_t shiftAmount = -14 -exponent; // e_min - exponent
        if (shiftAmount < 32) {
            // Add implicit bit of normalised binary32.  The input must be
            // normalised because if the input was a subnormal binary32 the
            // shiftAmount would be >= (-14 - (-127)) == 113
            truncatedSignificand = (1 << 23) | significandBits;

            // We need to check if any of the significand bits we shift out
            // are non zero so the "sticky bit" is set correctly.
            stickyBit = ((1 << shiftAmount) -1) & truncatedSignificand;

            // Now shift to the right
            truncatedSignificand >>= shiftAmount;
        } else {
            // do not overshift which is undefined behaviour in C
            stickyBit = significandBits > 0;
            truncatedSignificand = 0;
        }
        exponent = -15; // e_min -1. Fake exponent so that reEncoded exponent will be 0.
    } else if (exponent > 15) {
        // **Overflow**
        // The floating point number is larger than the largest normalised
        // number as binary16.  Use the largest value for the
        // truncatedSignificand This will cause the sticky bit and round bit set
        // to be set to true which will cause correct rounding behavior (i.e we
        // don't always round to +/- infinity, e.g. when doing
        // halide_toward_negative_infinity the result can **never** be
        // +infinity)
        truncatedSignificand = signMask | 0x007fffff;
        exponent = 15; // pretend the exponent is e_max
    } else {
        truncatedSignificand = significandBits;
    }

    // Before truncating the significand compute the round bit and sticky bit.
    // These are used to make rounding decisions later on.
    //
    // The roundBit is the single bit after truncation boundary
    // It represents half the value of the least significant bit in the final
    // half floating point number.
    bool roundBit = (truncatedSignificand & 0x001000) > 0;
    // The sticky bit is 1 if any of the bits after the "round bit"
    // are one. Note we OR with existing value because it may have been
    // set earlier when converting a normalised binary32 to a subnormal
    // binary16.
    stickyBit |= (truncatedSignificand & 0x000fff) > 0;

    // Now finally truncate the significand
    truncatedSignificand &= 0x7fe000;
    // Move the bits into the right position
    truncatedSignificand >>= (23 -10);

    uint32_t reEncodedExponent = exponent + 15;
    reEncodedExponent <<= 10; // Move bits into right position
    uint16_t result = signMask | reEncodedExponent | truncatedSignificand;

    return performRounding(result, roundBit, stickyBit, signMask);
}

// FIXME: This function and halide_float_float16_bits() are very similar
// they logic is the same but they have different types and constants.
// We should refactor these some how...
WEAK uint16_t halide_double_to_float16_bits(double value) {
   union {
        double asDouble;
        uint64_t asUInt;
    } data;
    // FIXME: Need C++11 to use this
    //static_assert(sizeof(double) == sizeof(uint64_t), "double is wrong size");
    data.asDouble = value;
    uint64_t bits = data.asUInt;
    uint64_t signMask = (bits & 0x8000000000000000) >> (63 - 15);
    int64_t exponent = (bits & 0x7ff0000000000000) >> 52; // Shift into position
    exponent -= 1023; // binary64 exponent is stored as bias-1023
    uint64_t significandBits = (bits & 0x000fffffffffffff);

    // e_max + 1 for binary64
    if (exponent == 1024 ) {
        if (significandBits == 0) {
            // +ve/-ve infinity
            return signMask | (0x1f << 10);
        } else {
            // NaN
            // Should we be dropping the sign?
            return 0x7e00;
        }
    }

    uint64_t truncatedSignificand=0;
    bool stickyBit = false;
    if (exponent <= -15) {
        // The floating point number is subnormal
        // as binary16 or zero
        //
        // Convert into subnormal form for binary 16
        // e.g.
        // 1.1 *2^-16 ==> 0.011* 2^-14
        int64_t shiftAmount = -14 -exponent; // e_min - exponent
        if (shiftAmount < 64) {
            // Add implicit bit of normalised binary32.  The input must be
            // normalised because if the input was a subnormal binary32 the
            // shiftAmount would be >= (-14 - (-127)) == 113
            truncatedSignificand = (1LL << 52) | significandBits;

            // We need to check if any of the significand bits we shift out
            // are non zero so the "sticky bit" is set correctly.
            stickyBit = ((1 << shiftAmount) -1) & truncatedSignificand;

            // Now shift to the right
            truncatedSignificand >>= shiftAmount;
        } else {
            // do not overshift which is undefined behaviour in C
            stickyBit = significandBits > 0;
            truncatedSignificand = 0;
        }
        exponent = -15; // e_min -1. Fake exponent so that reEncoded exponent will be 0.
    } else if (exponent > 15) {
        // **Overflow**
        // The floating point number is larger than the largest normalised
        // number as binary16.  Use the largest value for the
        // truncatedSignificand This will cause the sticky bit and round bit set
        // to be set to true which will cause correct rounding behavior (i.e we
        // don't always round to +/- infinity, e.g. when doing
        // halide_toward_negative_infinity the result can **never** be
        // +infinity)
        truncatedSignificand = signMask | 0x000fffffffffffff;
        exponent = 15; // pretend the exponent is e_max
    } else {
        truncatedSignificand = significandBits;
    }

    // Before truncating the significand compute the round bit and sticky bit.
    // These are used to make rounding decisions later on.
    //
    // The roundBit is the single bit after truncation boundary
    // It represents half the value of the least significant bit in the final
    // half floating point number.
    bool roundBit = (truncatedSignificand & 0x0000020000000000) > 0;
    // The sticky bit is 1 if any of the bits after the "round bit"
    // are one. Note we OR with existing value because it may have been
    // set earlier when converting a normalised binary32 to a subnormal
    // binary16.
    stickyBit |= (truncatedSignificand & 0x000001ffffffffff);

    // Now finally truncate the significand
    truncatedSignificand &= 0x000ffc0000000000;
    // Move the bits into the right position
    truncatedSignificand >>= (52 -10);

    uint32_t reEncodedExponent = exponent + 15;
    reEncodedExponent <<= 10; // Move bits into right position
    uint16_t result = signMask | reEncodedExponent | truncatedSignificand;

    return performRounding(result, roundBit, stickyBit, signMask);
}


}
