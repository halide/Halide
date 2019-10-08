#ifndef HALIDE_SIMPLIFY_CORRELATED_DIFFERENCES
#define HALIDE_SIMPLIFY_CORRELATED_DIFFERENCES

#include "IR.h"

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
 * nodes with arguments of opposite monotonicity. Only index
 * expressions outside of Stores/Provides are considered.
 *
 * This pass is safe to use on code with repeated instances of the
 * same variable name (it must be, because we want to run it before
 * allocation bounds inference).
 */
Stmt simplify_correlated_differences(const Stmt &);

}  // namespace Internal
}  // namespace Halide

#endif
