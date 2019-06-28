#ifndef HALIDE_LERP_H
#define HALIDE_LERP_H

/** \file
 * Defines methods for converting a lerp intrinsic into Halide IR.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Build Halide IR that computes a lerp. Use by codegen targets that
 * don't have a native lerp. */
Expr lower_lerp(Expr zero_val, Expr one_val, Expr weight);

}  // namespace Internal
}  // namespace Halide

#endif
