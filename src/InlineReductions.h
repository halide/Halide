#ifndef HALIDE_INLINE_REDUCTIONS_H
#define HALIDE_INLINE_REDUCTIONS_H

#include "IR.h"
#include "RDom.h"
#include "Tuple.h"

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
 * An example using \ref sum :
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
Expr sum(Expr, const std::string &s = "sum");
Expr product(Expr, const std::string &s = "product");
Expr maximum(Expr, const std::string &s = "maximum");
Expr minimum(Expr, const std::string &s = "minimum");
//@}

/** Variants of the inline reduction in which the RDom is stated
 * explicitly. The expression can refer to multiple RDoms, and only
 * the inner one is captured by the reduction. This allows you to
 * write expressions like:
 \code
 RDom r1(0, 10), r2(0, 10), r3(0, 10);
 Expr e = minimum(r1, product(r2, sum(r3, r1 + r2 + r3)));
 \endcode
*/
// @{
Expr sum(RDom, Expr, const std::string &s = "sum");
Expr product(RDom, Expr, const std::string &s = "product");
Expr maximum(RDom, Expr, const std::string &s = "maximum");
Expr minimum(RDom, Expr, const std::string &s = "minimum");
// @}

/** Returns an Expr or Tuple representing the coordinates of the point
 * in the RDom which minimizes or maximizes the expression. The
 * expression must refer to some RDom. Also returns the extreme value
 * of the expression as the last element of the tuple. */
// @{
Tuple argmax(Expr, const std::string &s = "argmax");
Tuple argmin(Expr, const std::string &s = "argmin");
Tuple argmax(RDom, Expr, const std::string &s = "argmax");
Tuple argmin(RDom, Expr, const std::string &s = "argmin");
// @}

}  // namespace Halide

#endif
