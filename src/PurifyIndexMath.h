#ifndef HALIDE_PURIFY_INDEX_MATH_H
#define HALIDE_PURIFY_INDEX_MATH_H

/** \file
 * Removes side-effects in integer math.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Bounds inference and related stages can lift integer bounds
 * expressions out of if statements that guard against those integer
 * expressions doing side-effecty things like dividing or modding by
 * zero. In those cases, if the lowering passes are functional, the
 * value resulting from the division or mod is evaluated but not
 * used. This mutator rewrites divs and mods in such expressions to
 * fail silently (evaluate to undef) when the denominator is zero.
 */
Expr purify_index_math(Expr);

}
}

#endif
