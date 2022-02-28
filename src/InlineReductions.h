#ifndef HALIDE_INLINE_REDUCTIONS_H
#define HALIDE_INLINE_REDUCTIONS_H

#include <string>

#include "Expr.h"
#include "RDom.h"
#include "Tuple.h"

/** \file
 * Defines some inline reductions: sum, product, minimum, maximum.
 */
namespace Halide {

class Func;

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
Expr saturating_sum(Expr, const std::string &s = "saturating_sum");
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
Expr sum(const RDom &, Expr, const std::string &s = "sum");
Expr saturating_sum(const RDom &r, Expr e, const std::string &s = "saturating_sum");
Expr product(const RDom &, Expr, const std::string &s = "product");
Expr maximum(const RDom &, Expr, const std::string &s = "maximum");
Expr minimum(const RDom &, Expr, const std::string &s = "minimum");
// @}

/** Returns an Expr or Tuple representing the coordinates of the point
 * in the RDom which minimizes or maximizes the expression. The
 * expression must refer to some RDom. Also returns the extreme value
 * of the expression as the last element of the tuple. */
// @{
Tuple argmax(Expr, const std::string &s = "argmax");
Tuple argmin(Expr, const std::string &s = "argmin");
Tuple argmax(const RDom &, Expr, const std::string &s = "argmax");
Tuple argmin(const RDom &, Expr, const std::string &s = "argmin");
// @}

/** Inline reductions create an anonymous helper Func to do the
 * work. The variants below instead take a named Func object to use,
 * so that it is no longer anonymous and can be scheduled
 * (e.g. unrolled across the reduction domain). The Func passed must
 * not have any existing definition. */
//@{
Expr sum(Expr, const Func &);
Expr saturating_sum(Expr, const Func &);
Expr product(Expr, const Func &);
Expr maximum(Expr, const Func &);
Expr minimum(Expr, const Func &);
Expr sum(const RDom &, Expr, const Func &);
Expr saturating_sum(const RDom &r, Expr e, const Func &);
Expr product(const RDom &, Expr, const Func &);
Expr maximum(const RDom &, Expr, const Func &);
Expr minimum(const RDom &, Expr, const Func &);
Tuple argmax(Expr, const Func &);
Tuple argmin(Expr, const Func &);
Tuple argmax(const RDom &, Expr, const Func &);
Tuple argmin(const RDom &, Expr, const Func &);
//@}

}  // namespace Halide

#endif
