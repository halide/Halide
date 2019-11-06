#ifndef HALIDE_SIMPLIFY_H
#define HALIDE_SIMPLIFY_H

/** \file
 * Methods for simplifying halide statements and expressions
 */

#include "Bounds.h"
#include "IR.h"
#include "ModulusRemainder.h"

namespace Halide {
namespace Internal {

/** Perform a a wide range of simplifications to expressions and
 * statements, including constant folding, substituting in trivial
 * values, arithmetic rearranging, etc. Simplifies across let
 * statements, so must not be called on stmts with dangling or
 * repeated variable names.
 */
// @{
Stmt simplify(Stmt, bool remove_dead_lets = true,
              const Scope<Interval> &bounds = Scope<Interval>::empty_scope(),
              const Scope<ModulusRemainder> &alignment = Scope<ModulusRemainder>::empty_scope());
Expr simplify(Expr, bool remove_dead_lets = true,
              const Scope<Interval> &bounds = Scope<Interval>::empty_scope(),
              const Scope<ModulusRemainder> &alignment = Scope<ModulusRemainder>::empty_scope());
// @}

/** Attempt to statically prove an expression is true using the simplifier. */
bool can_prove(Expr e, const Scope<Interval> &bounds = Scope<Interval>::empty_scope());

/** Simplify expressions found in a statement, but don't simplify
 * across different statements. This is safe to perform at an earlier
 * stage in lowering than full simplification of a stmt. */
Stmt simplify_exprs(Stmt);

}  // namespace Internal
}  // namespace Halide

#endif
