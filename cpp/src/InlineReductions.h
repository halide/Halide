#ifndef HALIDE_INLINE_REDUCTIONS_H
#define HALIDE_INLINE_REDUCTIONS_H

#include "IR.h"

/** \file
 * Defines some inline reductions: sum, product, minimum, maximum.
 */ 
namespace Halide {

/** An inline reduction. This is suitable for convolution-type
 * operations - the reduction will be computed in the innermost loop
 * that it is used in. The argument may contain free or implicit
 * variables, and must refer to some reduction domain. The free
 * variables are still free in the return value, but the reduction
 * domain is captured - the result expression does not refer to a
 * reduction domain and can be used in a pure function definition.
 * 
 * An example:
 * 
 \code
 Func f, g;
 Var x;
 RDom r(0, 10);
 f(x) = x*x;
 g(x) = sum(f(x + r));
 \endcode
 *
 * Here g computes some blur of x, but g is still a pure function. The
 * sum is being computed by an anonymous reduction function that is
 * scheduled innermost within g.
 */
//@{
EXPORT Expr sum(Expr);
EXPORT Expr product(Expr);
EXPORT Expr maximum(Expr);
EXPORT Expr minimum(Expr);
//@}

}

#endif
