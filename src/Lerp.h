#ifndef HALIDE_LERP_H
#define HALIDE_LERP_H

/** \file
 * Defines methods for converting a lerp intrinsic into Halide IR.
 */

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

/** Build Halide IR that computes a lerp. Use by codegen targets that don't have
 * a native lerp. The lerp is done in the type of the zero value. The final_type
 * is a cast that should occur after the lerp. It's included because in some
 * cases you can incorporate a final cast into the lerp math. */
Expr lower_lerp(Type final_type, Expr zero_val, Expr one_val, const Expr &weight, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
