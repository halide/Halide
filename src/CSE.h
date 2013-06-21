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
* This is important to do within Halide (instead of punting to llvm),
 * because exprs that come in from the front-end are small when
 * considered as a graph, but combinatorially large when considered as
 * a tree. For an example of a such a case, see
 * test/code_explosion.cpp */

Expr common_subexpression_elimination(Expr);

/** Do common-subexpression-elimination on each expression in a
 * statement. Does not introduce let statements. */
Stmt common_subexpression_elimination(Stmt);

/** Remove all lets from a statement or expression by substituting
 * them in. All sub-expressions will exist once in memory, but may
 * have many pointers to them, so this doesn't cause a combinatorial
 * explosion. If you walk over this as if it were a tree, however,
 * you're going to have a bad time. */
// @{
Expr remove_lets(Expr);
Stmt remove_lets(Stmt);
// @}

}
}

#endif
