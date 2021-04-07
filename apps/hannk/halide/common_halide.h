// A collection of utility functions shared by the halide generators.

#ifndef HANNK_COMMON_HALIDE_H
#define HANNK_COMMON_HALIDE_H

#include "Halide.h"

namespace hannk {

using Halide::Internal::rounding_shift_right;
using Halide::Internal::saturating_add;
using Halide::Internal::saturating_sub;
using Halide::Internal::widening_add;
using Halide::Internal::widening_mul;
using Halide::Internal::widening_sub;

// Get the number of vector registers available on the target.
int get_register_count(const Halide::Target &target);

// A tensor has the same requirements as a buffer in Halide by default, except
// the min of the innermost dimension must also be 0.
void interpret_as_tensor(Halide::OutputImageParam p);

// Require dimension dim have the same min and extent.
void require_same_min_extent(int dim, Halide::OutputImageParam first, Halide::OutputImageParam second);

Halide::Expr is_interleaved(Halide::OutputImageParam p, int channels);

// Round x down or up to the nearest multiple of n.
Halide::Expr align_down(const Halide::Expr &x, const Halide::Expr &n);
Halide::Expr align_up(const Halide::Expr &x, const Halide::Expr &n);
Halide::Expr align(const Halide::Expr &x, const Halide::Expr &n);

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
Halide::Expr multiply_2x_high(const Halide::Expr &a, const Halide::Expr &b);

// Performs right shift and multiply by a multiplier. Aims to be very close to
// tflite's reference implementation. However, tflite is standardizing on left
// (exponent-like) shifts.
Halide::Expr multiply_quantized(
    const Halide::Expr &x, const Halide::Expr &quantized_multiplier, const Halide::Expr &shift);

// Approximately compute log2(x)*2^log2_precision.
Halide::Expr approx_log2(const Halide::Expr &x, int log2_precision);

// Approximately compute (2^(x>>log2_precision_x))<<log2_precision_result.
// This approximation is a piecewise linear curve passing through each exact
// power of 2.
Halide::Expr approx_exp2(const Halide::Expr &x, const Halide::Expr &log2_precision_x, int log2_precision_result);

}  // namespace hannk

#endif  // HANNK_COMMON_HALIDE_H
