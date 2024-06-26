// A collection of utility functions shared by the halide generators.

#ifndef HANNK_COMMON_HALIDE_H
#define HANNK_COMMON_HALIDE_H

#include "Halide.h"

namespace hannk {

using Halide::rounding_shift_right;
using Halide::saturating_add;
using Halide::saturating_sub;
using Halide::widening_add;
using Halide::widening_mul;
using Halide::widening_sub;

// Get the number of vector registers available on the target.
int get_register_count(const Halide::Target &target);

// Get the vector reduction factor that is convenient for this target
// for the given type.
int get_vector_reduction_factor(const Halide::Target &target, Halide::Type t);

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

// Compute saturating_narrow(rounding_shift_right(widening_mul(a, b), N))
// where N is the number of bits of the narrowed result minus one.
Halide::Expr multiply_2x_high(const Halide::Expr &a, const Halide::Expr &b);

// For a visualization of the approx_* functions and their errors, see:
// apps/hannk/halide/docs/approx_log2_and_applications.ipynb
// Approximate log2(x/2^q_x)*2^q.
// q must be less than 16.
Halide::Expr approx_log2(int q, const Halide::Expr &x, int q_x, const Halide::Type &type = Halide::Int(32));

// Approximate 2^(x/2^q_x)/2^q.
// q_x must be less than 16.
Halide::Expr approx_exp2(int q, const Halide::Expr &x, const Halide::Expr &q_x, const Halide::Type &type = Halide::Int(32));

// Approximate 2^q/x
Halide::Expr approx_reciprocal(int q, const Halide::Expr &x, const Halide::Type &type = Halide::Int(32));

// Approximate 2^q/sqrt(x)
Halide::Expr approx_reciprocal_sqrt(int q, const Halide::Expr &x, const Halide::Type &type = Halide::Int(32));

// Approximate 2^q*log2(2^(x/2^q_x) +/- 1)
Halide::Expr approx_log2p1_exp2(int q, const Halide::Expr &x, const Halide::Expr &q_x, const Halide::Type &type = Halide::Int(32));
Halide::Expr approx_log2m1_exp2(int q, const Halide::Expr &x, const Halide::Expr &q_x, const Halide::Type &type = Halide::Int(32));

// Approximate 2^q/(1 + 2^(-x/2^q_x))
Halide::Expr approx_logistic(int q, const Halide::Expr &x, const Halide::Expr &q_x, const Halide::Type &type = Halide::Int(32));
// Approximate 2^q*tanh(x/2^q_x)
Halide::Expr approx_tanh(int q, const Halide::Expr &x, const Halide::Expr &q_x, const Halide::Type &type = Halide::Int(32));

// Compute i16(x * multiplier >> shift). The optimal expression for this may depend on the target.
Halide::Expr quantize_i16(const Halide::Expr &x, const Halide::Expr &multiplier, const Halide::Expr &shift, const Halide::Target &target);

// Compute u8(clamp((x * multiplier >> shift) + zero, min, max)). The optimal expression for this may depend on the target.
Halide::Expr quantize_and_relu_u8(const Halide::Expr &x, const Halide::Expr &multiplier, const Halide::Expr &shift, const Halide::Expr &zero,
                                  const Halide::Expr &min, const Halide::Expr &max, const Halide::Target &target);

}  // namespace hannk

#endif  // HANNK_COMMON_HALIDE_H
