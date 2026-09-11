#ifndef HALIDE_INJECT_MODULO_VARS_H
#define HALIDE_INJECT_MODULO_VARS_H

/** \file
 * Defines a pass that makes the value of a variable modulo a constant visible
 * at the places where only that much of it is needed.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Find variables used only modulo a constant -- x % 5, (x + 3) % 5, and
 * (x + 2*y) % 3 all need no more of x than x % 5 -- and bind that reduced
 * value to a new variable next to the let that defines the original. Uses of
 * the original in those positions are then replaced by the new variable.
 *
 * Reducing a value where it is defined often collapses it. A loop variable
 * reconstructed by an aligned split looks like xo * 16 + offset, which modulo
 * two is just offset. The simplifier can't discover that at the use site: a
 * LetStmt hides the value behind a name, and it won't substitute a whole
 * expression back in. It will substitute a variable, so giving the use site a
 * name bound to the reduced value is enough. (x - offset) % 2 becomes
 * (x.mod.2 - offset) % 2, and with x.mod.2 bound to offset that folds to zero.
 *
 * Only reductions that collapse to a constant or a single variable are kept,
 * so this never grows the IR. */
Stmt inject_modulo_vars(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
