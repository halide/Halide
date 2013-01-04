#ifndef HALIDE_INLINE_REDUCTIONS_H
#define HALIDE_INLINE_REDUCTIONS_H

#include "IR.h"

namespace Halide {

/* Some inline reductions. They expect the expression argument to
 * refer to some reduction domain. They may contain free variables.
 * E.g.:
 * Func f, g;
 * Var x;
 * RDom r(0, 10);
 * f(x) = x*x;
 * g(x) = sum(f(x + r));
 */

Expr sum(Expr);
Expr product(Expr);
Expr maximum(Expr);
Expr minimum(Expr);

}

#endif
