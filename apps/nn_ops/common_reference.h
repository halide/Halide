// A collection of utility functions shared by test apps.

#ifndef COMMON_REFERENCE_H_
#define COMMON_REFERENCE_H_

#include <cstdint>

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
int32_t saturating_rounding_doubling_high_multiply_reference(int32_t a, int32_t b);

// Correctly-rounded-to-nearest division by a power-of-two. Also known as
// rounding arithmetic right shift.
int32_t rounding_shift_right_reference(int32_t x, int32_t shift);

// Performs right shift and multiply by a multiplier.
int32_t multiply_quantized_multiplier_reference(int32_t x, int32_t q, int32_t shift);

#endif
