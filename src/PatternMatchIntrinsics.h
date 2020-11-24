#ifndef HALIDE_PATTERN_MATCH_INTRINSICS_H
#define HALIDE_PATTERN_MATCH_INTRINSICS_H

/** \file
 * Tools to replace common patterns with more readily recognizable intrinsics.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Replace common arithmetic patterns with intrinsics. */
Stmt pattern_match_intrinsics(Stmt s);

/** Implement intrinsics with non-intrinsic using equivalents. */
Expr lower_widening_add(Expr a, Expr b);
Expr lower_widening_subtract(Expr a, Expr b);
Expr lower_widening_multiply(Expr a, Expr b);

Expr lower_rounding_shift_right(Expr a, Expr b);
Expr lower_rounding_shift_left(Expr a, Expr b);

Expr lower_saturating_add(Expr a, Expr b);
Expr lower_saturating_subtract(Expr a, Expr b);

Expr lower_halving_add(Expr a, Expr b);
Expr lower_rounding_halving_add(Expr a, Expr b);
Expr lower_halving_subtract(Expr a, Expr b);
Expr lower_rounding_halving_subtract(Expr a, Expr b);

}  // namespace Internal
}  // namespace Halide

#endif
