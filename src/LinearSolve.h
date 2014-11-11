#ifndef HALIDE_LINEAR_SOLVE_H
#define HALIDE_LINEAR_SOLVE_H

/** \file
 * Defines a method for solving linear equations for a given variable.
 */

#include "IR.h"
#include "Scope.h"
#include "Var.h"

namespace Halide {
namespace Internal {

/**
 * Returns true if the input Expr is linear in the named variable, or in any of the free
 * variables contained in the first scope argument. We say that an expression is linear
 * if at least one of the variables appear in the expression and at most one free variable
 * appears in each linear term. So expressions constant in the variables are not considered
 * linear. The second scope argument contains variables that are bound to linear expressions
 * in the free variables.
 */
// @{
bool expr_is_linear_in_var(Expr expr, const std::string &var);
bool expr_is_linear_in_vars(Expr expr, const Scope<int> &free_vars);
bool expr_is_linear_in_vars(Expr expr, const Scope<int> &free_vars, const Scope<bool> &bound_vars);
// @}

/**
 * A struct that represents a simple term in an expression. The term
 * is defined as the coefficient times the variable. If the variable
 * is a null pointer, then the term is a constant value equal to the
 * coefficient.
 */
struct Term {
  Expr coeff;
  const Variable* var;
};

/**
 * This function collects the terms of a linear expression into an
 * array. The function will return true if the expression is indeed
 * linear, and so this can be used to detect linear expressions as
 * well.
 */
// @{
EXPORT bool collect_linear_terms(Expr e, std::vector<Term> &terms,
                                 const Scope<int> &free_vars);

EXPORT bool collect_linear_terms(Expr e, std::vector<Term> &terms,
                                 const Scope<int> &free_vars,
                                 const Scope<Expr> &scope);
// @}

/**
 * This function solves a conditional expression made up of linear
 * expressions for a particular variable. If the expression contains
 * logical conjunctives, then each proposition is solved
 * independently. It returns the solved expression if it succeeds,
 * otherwise it returns the inpute expression [e].
 */
// @{
EXPORT Expr solve_for_linear_variable(Expr e, Var x,
                                      const Scope<int> &free_vars);

EXPORT Expr solve_for_linear_variable(Expr e, Var x,
                                      const Scope<int> &free_vars,
                                      const Scope<Expr> &scope);
// @}

}
}

#endif
