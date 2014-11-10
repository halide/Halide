#ifndef HALIDE_BRANCH_UTILS_H
#define HALIDE_BRANCH_UTILS_H

/** \file
 * Defines several IR mutators and visitors that detect and modify the branching structure
 * of the IR. These are mostly used by specialize_branched_loops.
 */

#include "IR.h"
#include "Bounds.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/**
 * Returns true if [stmt] or [expr] branches in the variable [var],
 * given the bound expressions in [scope].  The last argument,
 * [branch_on_minmax], can be set to true if you wish to consider
 * min/max expressions as branch points.
 */
// @(
EXPORT bool branches_in_var(Stmt stmt, const std::string &var, const Scope<Expr> &scope,
                            bool branch_on_minmax = false);
EXPORT bool branches_in_var(Expr expr, const std::string &var, const Scope<Expr> &scope,
                            bool branch_on_minmax = false);
// @}

/**
 * Prune the branches in [stmt] or [expr] relative to the variable
 * [var], considering the bounds provided by [bounds].  Branching
 * conditions are used to modify the bounds on [var], and thus we can
 * potentially reduce some of the nested branching structure. Here is
 * an example:
 *
 *   if (x < 0) {
 *      if (x < 1) {
 *          ...
 *      }
 *   }
 *
 * Will be reduced to:
 *
 *   if (x < 0) {
 *     ...
 *   }
 *
 * The final argument [vars] is a scope containing all the free variables.
 */
// @{
EXPORT Stmt prune_branches(Stmt stmt, const std::string &var, const Scope<Expr> &scope,
                           const Scope<Interval> &bounds, const Scope<int> &vars);

EXPORT Expr prune_branches(Expr expr, const std::string &var, const Scope<Expr> &scope,
                           const Scope<Interval> &bounds, const Scope<int> &vars);
// @}

/**
 * Normalize the branching conditions in IfThenElse and Select
 * nodes. By which we mean, reduce the condition to a simple
 * inequality expression if possible. Equality/inequality conditions
 * are converted into compound expressions involving inequalities and
 * all logical connectives are removed from the conditions. We end up
 * with a nested tree of branches, which can then be pruned by
 * [prune_branches]. Here is an example:
 *
 *   if (x <= 10 && x != 5) {
 *      ...
 *   }
 *
 * will get mutated into:
 *
 *   if (x <= 10) {
 *     if (x < 5) {
 *       ...
 *     } else if (x > 5) {
 *       ...
 *     }
 *  }
 *
 */
// @{
EXPORT Stmt normalize_branch_conditions(Stmt stmt, const Scope<Expr> &scope);
EXPORT Expr normalize_branch_conditions(Expr expr, const Scope<Expr> &scope);
// @}

}
}

#endif
