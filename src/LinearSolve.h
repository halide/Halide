#ifndef HALIDE_SOLVE_H
#define HALIDE_SOLVE_H

/** \file
 * Defines a lowering pass that simplifies code using clamped ramps.
 */

#include "IR.h"
#include "Scope.h"
#include "Var.h"

namespace Halide {
namespace Internal {

/* A struct that represents a simple term in an expression. The term
 * is defined as the coefficient times the variable. If the variable
 * is a null pointer, then the term is a constant value equal to the
 * coefficient.
 */
struct Term {
  Expr coeff;
  const Variable* var;
};

/* This function collects the terms of a linear expression into an
 * array. The function will return true if the expression is indeed
 * linear, and so this can be used to detect linear expressions as
 * well.
 */
bool collect_linear_terms(Expr e, std::vector<Term>& terms);
bool collect_linear_terms(Expr e, std::vector<Term>& terms, const Scope<Expr>& scope);

/* This function solves a conditional expression made up of linear
 * expressions for a particular variable. If the expression contains
 * logical conjunctives, then each proposition is solved
 * independently. It returns the solved expression if it succeeds,
 * otherwise it returns the inpute expression [e].
 */
Expr solve_for_linear_variable(Expr e, Var x);
Expr solve_for_linear_variable(Expr e, Var x, const Scope<Expr>& scope);

}
}

#endif
