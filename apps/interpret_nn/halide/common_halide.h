// A collection of utility functions shared by the halide generators.

#ifndef COMMON_HALIDE_H_
#define COMMON_HALIDE_H_

#include "Halide.h"

namespace interpret_nn {

// A tensor has the same requirements as a buffer in Halide by default, except
// the min of the innermost dimension must also be 0.
void interpret_as_tensor(Halide::OutputImageParam p);

// Require that the first two dimensions of two buffers have the same bounds.
void require_same_extent_cx(Halide::OutputImageParam first, Halide::OutputImageParam second);
void require_same_extent_b(Halide::OutputImageParam first, Halide::OutputImageParam second);

// Check if the first two dimensions of a buffer can be fused cleanly.
Halide::Expr can_fuse_cx(Halide::OutputImageParam p);

// A boundary condition, without likelies that cause loop partitioning.
Halide::Func constant_exterior_tensor(
    Halide::Func t, Halide::Expr exterior,
    Halide::Expr min_c, Halide::Expr extent_c,
    Halide::Expr min_x, Halide::Expr extent_x,
    Halide::Expr min_y, Halide::Expr extent_y,
    Halide::Expr min_b, Halide::Expr extent_b);
Halide::Func constant_exterior_tensor(Halide::ImageParam p, Halide::Expr exterior);

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
Halide::Expr multiply_2x_high(const Halide::Expr &a, const Halide::Expr &b);

// Correctly-rounded-to-nearest division by a power-of-two. Also known as
// rounding arithmetic right shift.
Halide::Expr round_shift_right(const Halide::Expr &x, const Halide::Expr &shift);

// Performs right shift and multiply by a multiplier. Aims to be very close to
// tflite's reference implementation. However, tflite is standardizing on left
// (exponent-like) shifts.
Halide::Expr multiply_quantized(
    const Halide::Expr &x, const Halide::Expr &quantized_multiplier, const Halide::Expr &shift);

}  // namespace interpret_nn

#endif  // COMMON_HALIDE_H_
