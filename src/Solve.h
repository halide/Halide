#ifndef SOLVE_H
#define SOLVE_H

/** Defines methods for manipulating and analyzing boolean expressions. */

#include "IR.h"
#include "Scope.h"
#include "Bounds.h"

namespace Halide {
namespace Internal {

/** Attempts to collect all instances of a variable in an expression
 * tree and place it as far to the left as possible, and as far up the
 * tree as possible (i.e. outside most parentheses). If the expression
 * is an equality or comparison, this 'solves' the equation. Returns
 * an undefined expression on failure. */
EXPORT Expr solve_expression(Expr e, const std::string &variable,
                             const Scope<Expr> &scope = Scope<Expr>::empty_scope());

/** Find the smallest interval such that the condition is either true
 * or false inside of it, but definitely false outside of it. Never
 * returns undefined Exprs, instead it uses variables called "pos_inf"
 * and "neg_inf" to represent positive and negative infinity. */
EXPORT Interval solve_for_outer_interval(Expr c, const std::string &variable);

/** Find the largest interval such that the condition is definitely
 * true inside of it, and might be true or false outside of it. */
EXPORT Interval solve_for_inner_interval(Expr c, const std::string &variable);

/** Check properties of the intervals returned by
 * solve_for_inner_interval or solve_for_outer_interval. */
// @{
EXPORT bool interval_has_upper_bound(const Interval &i);
EXPORT bool interval_has_lower_bound(const Interval &i);
EXPORT bool interval_is_everything(const Interval &i);
EXPORT bool interval_is_empty(const Interval &i);
// @}

/** Take a conditional that includes variables that vary over some
 * domain, and convert it to a more conservative (less frequently
 * true) condition that doesn't depend on those variables. Formally,
 * the output expr implies the input expr.
 *
 * The condition may be a vector condition, in which case we also
 * 'and' over the vector lanes, and return a scalar result. */
Expr and_condition_over_domain(Expr c, const Scope<Interval> &varying);

EXPORT void solve_test();

}
}

#endif
