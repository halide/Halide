#ifndef HALIDE_REVERSE_PEEL_H
#define HALIDE_REVERSE_PEEL_H

/** \file
 * Defines the lowering pass that gives arithmetic shared by several uses of a
 * let-bound variable a let of its own.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** The simplifier peels cheap operations (+, -, *, /, %, min, max against a
 * constant or a variable) off the value of a let and pushes them into every use
 * of the variable, so that after the last simplification the same arithmetic is
 * repeated at each use site. This pass finds, per scalar let, the operation
 * chains its uses apply to the variable and binds every chain prefix shared by
 * two or more uses to a new variable, named after the original with a .rp
 * suffix. The new let is placed at the innermost loop or let body containing
 * all of its uses. When the variable is only ever used through one chain, that
 * chain is folded into the value of the original let instead. */
Stmt reverse_peel_lets(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
