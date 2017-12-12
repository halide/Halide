// A collection of utility functions shared by the halide generators.

#ifndef COMMON_HALIDE_H_
#define COMMON_HALIDE_H_

#include <Halide.h>

// Align the buffer to be to multiples of the vector size.
void RequireAligned(const int alignment, Halide::OutputImageParam* param);

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
Halide::Expr SaturatingRoundingDoublingHighMultiply(Halide::Expr a, Halide::Expr b);

// Correctly-rounded-to-nearest division by a power-of-two. Also known as
// rounding arithmetic right shift.
Halide::Expr RoundingShiftRight(Halide::Expr x, Halide::Expr shift);

// Performs right shift and multiply by a multiplier.
Halide::Expr MultiplyByQuantizedMultiplier(
    Halide::Expr x, Halide::Expr quantized_multiplier, Halide::Expr shift);

// Returns the natural vector size for the given data type T and hardware
// target. Only the following types are currently verified to work:
// uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t
template <typename T>
int NaturalVectorSize(const Halide::Target& target) {
}

#endif
