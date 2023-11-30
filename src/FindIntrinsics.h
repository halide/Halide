#ifndef HALIDE_FIND_INTRINSICS_H
#define HALIDE_FIND_INTRINSICS_H

/** \file
 * Tools to replace common patterns with more readily recognizable intrinsics.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Implement intrinsics with non-intrinsic using equivalents. */
Expr lower_widen_right_add(const Expr &a, const Expr &b);
Expr lower_widen_right_mul(const Expr &a, const Expr &b);
Expr lower_widen_right_sub(const Expr &a, const Expr &b);
Expr lower_widening_add(const Expr &a, const Expr &b);
Expr lower_widening_mul(const Expr &a, const Expr &b);
Expr lower_widening_sub(const Expr &a, const Expr &b);
Expr lower_widening_shift_left(const Expr &a, const Expr &b);
Expr lower_widening_shift_right(const Expr &a, const Expr &b);

Expr lower_rounding_shift_left(const Expr &a, const Expr &b);
Expr lower_rounding_shift_right(const Expr &a, const Expr &b);

Expr lower_saturating_add(const Expr &a, const Expr &b);
Expr lower_saturating_sub(const Expr &a, const Expr &b);
Expr lower_saturating_cast(const Type &t, const Expr &a);

Expr lower_halving_add(const Expr &a, const Expr &b);
Expr lower_halving_sub(const Expr &a, const Expr &b);
Expr lower_rounding_halving_add(const Expr &a, const Expr &b);
Expr lower_sorted_avg(const Expr &a, const Expr &b);

Expr lower_mul_shift_right(const Expr &a, const Expr &b, const Expr &q);
Expr lower_rounding_mul_shift_right(const Expr &a, const Expr &b, const Expr &q);

/** Replace one of the above ops with equivalent arithmetic. */
Expr lower_intrinsic(const Call *op);

/** Replace common arithmetic patterns with intrinsics. */
Stmt find_intrinsics(const Stmt &s);
Expr find_intrinsics(const Expr &e);

/** The reverse of find_intrinsics. */
Expr lower_intrinsics(const Expr &e);
Stmt lower_intrinsics(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
