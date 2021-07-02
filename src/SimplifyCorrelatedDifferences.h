#ifndef HALIDE_SIMPLIFY_CORRELATED_DIFFERENCES
#define HALIDE_SIMPLIFY_CORRELATED_DIFFERENCES

#include "Expr.h"

/** \file
 * Defines a simplification pass for handling differences of correlated expressions.
 */

namespace Halide {
namespace Internal {

/** Symbolic interval arithmetic can be extremely conservative in
 * cases where we analyze the difference between two correlated
 * expressions. For example, consider:
 *
 * for x in [0, 10]:
 *   let y = x + 3
 *   let z = y - x
 *
 * x lies within [0, 10]. Interval arithmetic will correctly determine
 * that y lies within [3, 13]. When z is encountered, it is treated as
 * a difference of two independent variables, and gives [3 - 10, 13 -
 * 0] = [-7, 13] instead of the tighter interval [3, 3]. It
 * doesn't understand that y and x are correlated.
 *
 * In practice, this problem causes problems for unrolling, and
 * arbitrarily-bad overconservative behavior in bounds inference
 * (e.g. https://github.com/halide/Halide/issues/3697 )
 *
 * The function below attempts to address this by walking the IR,
 * remembering whether each let variable is monotonic increasing,
 * decreasing, unknown, or constant w.r.t each loop var. When it
 * encounters a subtract node where both sides have the same
 * monotonicity it substitutes, solves, and attempts to generally
 * simplify as aggressively as possible to try to cancel out the
 * repeated dependence on the loop var. The same is done for addition
 * nodes with arguments of opposite monotonicity.
 *
 * Bounds inference is particularly sensitive to these false
 * dependencies, but removing false dependencies also helps other
 * lowering passes. E.g. if this simplification means a value no
 * longer depends on a loop variable, it can remain scalar during
 * vectorization of that loop, or we can lift it out as a loop
 * invariant, or it might avoid some of the complex paths in GPU
 * codegen that trigger when values depend on the block index
 * (e.g. warp shuffles).
 *
 * This pass is safe to use on code with repeated instances of the
 * same variable name (it must be, because we want to run it before
 * allocation bounds inference).
 */
Stmt simplify_correlated_differences(const Stmt &);

/** Refactor the expression to remove correlated differences
 * or rewrite them in a form that is more amenable to bounds
 * inference. Performs a subset of what `simplify_correlated_differences`
 * does. Can increase Expr size (i.e. does not follow the simplifier's
 * reduction order).
 */
Expr bound_correlated_differences(const Expr &expr);

}  // namespace Internal
}  // namespace Halide

#endif
