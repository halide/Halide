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
 * repeated at each use site. This pass finds, per scalar variable (a let, a loop
 * variable, or a free variable such as a parameter), the chains of pure nodes
 * its uses apply to it: arithmetic, comparisons, logic, selects, narrowing
 * casts and pure calls, where every other child of a node is a pure expression
 * available where the variable is bound. Pure expressions over constants alone
 * form chains of their own. Every chain prefix shared by two or more uses is
 * bound to a new variable, named after the original with a .rp suffix. A chain
 * used once is bound too when that moves it out of a loop. When a let-bound
 * variable is only ever used through one chain, that chain is folded into the
 * value of the original let instead.
 *
 * A new let goes at the innermost loop, let or if-branch body containing its
 * uses, unless that lies inside a loop the value is invariant in: then it goes
 * just around the outermost such loop, so the work is neither repeated per
 * iteration nor moved above anything else. Uses inside a gpu kernel get a let
 * inside the kernel, so kernels don't gain arguments. */
Stmt reverse_peel_lets(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
