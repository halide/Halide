#ifndef HALIDE_INTERNAL_CSE_H
#define HALIDE_INTERNAL_CSE_H

/** \file
 * Defines a pass for introducing let expressions to wrap common sub-expressions. */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Replace each common sub-expression in the argument with a
 * variable, and wrap the resulting expr in a let statement giving a
 * value to that variable.
 *
 * Full common-subexpression elimination will lift out many things
 * that the simplifier would just substitute back in. For example, the
 * simplifier will convert (let x = y+2 in (x*x)) to ((y+2)*(y+2)). By
 * default CSE does not make changes that the simplifier will just
 * undo. Set the second argument to false if you want to do full
 * common subexpression elimination.
 *
 * This is important to do within Halide (instead of punting to llvm),
 * because exprs that come in from the front-end are small when
 * considered as a graph, but combinatorially large when considered as
 * a tree. For an example of a such a case, see
 * test/code_explosion.cpp */
EXPORT Expr common_subexpression_elimination(Expr, bool conservative = true);

/** Do common-subexpression-elimination on each expression in a
 * statement. Does not introduce let statements. */
EXPORT Stmt common_subexpression_elimination(Stmt, bool conservative = true);

EXPORT void cse_test();

}
}

#endif
