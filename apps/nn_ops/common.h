// A collection of utility functions shared by the halide generators.

#ifndef COMMON_HALIDE_H_
#define COMMON_HALIDE_H_

#include <Halide.h>

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
Halide::Expr saturating_rounding_doubling_high_multiply(Halide::Expr a, Halide::Expr b);

// Correctly-rounded-to-nearest division by a power-of-two. Also known as
// rounding arithmetic right shift.
Halide::Expr rounding_shift_right(Halide::Expr x, Halide::Expr shift);

// Performs right shift and multiply by a multiplier.
Halide::Expr multiply_quantized_multiplier(
    Halide::Expr x, Halide::Expr quantized_multiplier, Halide::Expr shift);
#endif
