#ifndef SOLVE_H
#define SOLVE_H

/** Defines methods for solving equations. */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Attempts to collect all instances of a variable in an expression
 * tree and place it as far to the left as possible, and as far up the
 * tree as possible (i.e. outside most parentheses). If the expression
 * is an equality or comparison, this 'solves' the equation. Returns
 * an undefined expression on failure. */
EXPORT Expr solve_expression(Expr e, const std::string &variable, const Scope<Expr> &scope = Scope<Expr>());

EXPORT void solve_test();

}
}

#endif
