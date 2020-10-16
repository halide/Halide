// A collection of utility functions shared by the halide generators.

#ifndef COMMON_HALIDE_H_
#define COMMON_HALIDE_H_

#include <array>
#include <utility>

#include "Halide.h"

namespace interpret_nn {

// A tensor has the same requirements as a buffer in Halide by default, except
// the min of the innermost dimension must also be 0.
inline void InterpretAsTensor(Halide::OutputImageParam p) {
    p.dim(0).set_stride(1).set_min(0);
}

// Require that the first two dimensions of two buffers have the same bounds.
inline void RequireSameExtentCX(Halide::OutputImageParam first,
                                Halide::OutputImageParam second) {
    for (int d = 0; d < 2; d++) {
        second.dim(d).set_min(first.dim(d).min());
        second.dim(d).set_extent(first.dim(d).extent());
    }
}

// Check if the first two dimensions of a buffer can be fused cleanly.
inline Halide::Expr CanFuseCX(Halide::OutputImageParam p) {
  return p.dim(0).min() == 0 && p.dim(1).stride() > 0 &&
         p.dim(1).stride() == p.dim(0).extent();
}

// A boundary condition, without likelies that cause loop partitioning.
inline Halide::Func ConstantExteriorTensor(
    Halide::Func t, Halide::Expr exterior,
    Halide::Expr min_c, Halide::Expr extent_c,
    Halide::Expr min_x, Halide::Expr extent_x,
    Halide::Expr min_y, Halide::Expr extent_y,
    Halide::Expr min_b, Halide::Expr extent_b) {
    Halide::Var c("c"), x("x"), y("y"), b("b");
    // We usually don't care about what comes after the boundary in the c
    // or b dimensions, so just skip those for the select.
    Halide::Expr in_bounds =
        min_x <= x && x < min_x + extent_x &&
        min_y <= y && y < min_y + extent_y;
    Halide::Expr bounded("bounded");
    bounded = t(clamp(c, min_c, min_c + extent_c - 1),
                clamp(x, min_x, min_x + extent_x - 1),
                clamp(y, min_y, min_y + extent_y - 1),
                clamp(b, min_b, min_b + extent_b - 1));

    Halide::Func tensor_bounded("tensor_bounded");
    tensor_bounded(c, x, y, b) = select(in_bounds, bounded, exterior);

    return tensor_bounded;
}

inline Halide::Func ConstantExteriorTensor(Halide::ImageParam p,
                                           Halide::Expr exterior) {
    return ConstantExteriorTensor(p, exterior,
                                  p.dim(0).min(), p.dim(0).extent(),
                                  p.dim(1).min(), p.dim(1).extent(),
                                  p.dim(2).min(), p.dim(2).extent(),
                                  p.dim(3).min(), p.dim(3).extent());
}

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
Halide::Expr SaturatingRoundingDoublingHighMultiply(const Halide::Expr &a,
                                                    const Halide::Expr &b);

// This function implements the same computation as the gemmlowp function of
// the same name. The exponent should be positive. Only the following types are
// verified to work: int32_t
Halide::Expr SaturatingRoundingMultiplyByPOT(const Halide::Expr &x,
                                             const Halide::Expr &exponent);

// Correctly-rounded-to-nearest division by a power-of-two. Also known as
// rounding arithmetic right shift.
Halide::Expr RoundingDivideByPOT(const Halide::Expr &x,
                                 const Halide::Expr &shift);

// Performs right shift and multiply by a multiplier. Aims to be very close to
// tflite's reference implementation. However, tflite is standardizing on left
// (exponent-like) shifts.
Halide::Expr MultiplyByQuantizedMultiplierSmallerThanOne(
    const Halide::Expr &x, const Halide::Expr &quantized_multiplier,
    const Halide::Expr &shift);

// Performs the same operation as the tfmini function of the same name.
Halide::Expr MultiplyByQuantizedMultiplierGreaterThanOne(
    const Halide::Expr &input, const Halide::Expr &quantized_multiplier,
    const Halide::Expr &left_shift);

// Returns the multiplier and shift for a quantized inverse square root.
// Implements the same computation as tfmini's reference implementation. Only
// the following types are verified to work: int32_t
std::pair<Halide::Expr, Halide::Expr> InvSqrtQuantizedMultiplierExp(
    const Halide::Expr &start_input);

// This function implements the same computation as the gemmlowp function of the
// similar name. Only input values in the range [-(1 << 29), -1] are supported
// for int32 inputs. Only input values in the range [-(1 << 13), -1] are
// supported for int16 inputs.
Halide::Expr ExponentialOnNegativeOneQuarterAndZeroExclusive(
    const Halide::Expr &a);

}  // namespace interpret_nn

#endif  // COMMON_HALIDE_H_
