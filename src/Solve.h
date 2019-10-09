#ifndef SOLVE_H
#define SOLVE_H

/** Defines methods for manipulating and analyzing boolean expressions. */

#include "Bounds.h"
#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

struct SolverResult {
    Expr result;
    bool fully_solved;
};

/** Attempts to collect all instances of a variable in an expression
 * tree and place it as far to the left as possible, and as far up the
 * tree as possible (i.e. outside most parentheses). If the expression
 * is an equality or comparison, this 'solves' the equation. Returns a
 * pair of Expr and bool. The Expr is the mutated expression, and the
 * bool indicates whether there is a single instance of the variable
 * in the result. If it is false, the expression has only been partially
 * solved, and there are still multiple instances of the variable. */
SolverResult solve_expression(
    Expr e, const std::string &variable,
    const Scope<Expr> &scope = Scope<Expr>::empty_scope());

/** Find the smallest interval such that the condition is either true
 * or false inside of it, but definitely false outside of it. Never
 * returns undefined Exprs, instead it uses variables called "pos_inf"
 * and "neg_inf" to represent positive and negative infinity. */
Interval solve_for_outer_interval(Expr c, const std::string &variable);

/** Find the largest interval such that the condition is definitely
 * true inside of it, and might be true or false outside of it. */
Interval solve_for_inner_interval(Expr c, const std::string &variable);

/** Take a conditional that includes variables that vary over some
 * domain, and convert it to a more conservative (less frequently
 * true) condition that doesn't depend on those variables. Formally,
 * the output expr implies the input expr.
 *
 * The condition may be a vector condition, in which case we also
 * 'and' over the vector lanes, and return a scalar result. */
Expr and_condition_over_domain(Expr c, const Scope<Interval> &varying);

void solve_test();

}  // namespace Internal
}  // namespace Halide

#endif
