#ifndef HALIDE_SIMPLIFY_H
#define HALIDE_SIMPLIFY_H

/** \file
 * Methods for simplifying halide statements and expressions
 */

#include "Expr.h"
#include "Interval.h"
#include "ModulusRemainder.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Perform a wide range of simplifications to expressions and statements,
 * including constant folding, substituting in trivial values, arithmetic
 * rearranging, etc. Simplifies across let statements, so must not be called on
 * stmts with dangling or repeated variable names. Can optionally be passed
 * known bounds of any variables, known alignment properties, and any other
 * Exprs that should be assumed to be true.
 */
// @{
Stmt simplify(const Stmt &,
              const Scope<Interval> &bounds = Scope<Interval>::empty_scope(),
              const Scope<ModulusRemainder> &alignment = Scope<ModulusRemainder>::empty_scope(),
              const std::vector<Expr> &assumptions = std::vector<Expr>());
Expr simplify(const Expr &,
              const Scope<Interval> &bounds = Scope<Interval>::empty_scope(),
              const Scope<ModulusRemainder> &alignment = Scope<ModulusRemainder>::empty_scope(),
              const std::vector<Expr> &assumptions = std::vector<Expr>());
// @}

/** Attempt to statically prove an expression is true using the simplifier. */
bool can_prove(Expr e, const Scope<Interval> &bounds = Scope<Interval>::empty_scope());

/** Has lowering finished deriving regions and allocation sizes from the IR?
 *
 * A clamp around an index is not only a statement about a value: it is part of
 * how those are derived. Until they have been, the simplifier must not use a
 * condition it happens to know to remove one, or the region asked for grows to
 * whatever the unclamped index could reach. Afterwards the derived regions are
 * already IR of their own, and removing a redundant clamp is just a
 * simplification. */
bool regions_have_been_inferred();

/** Mark regions as derived for the rest of the enclosing scope. Lowering does
 * this once, after the last pass that reads a region out of the IR. */
struct ScopedRegionsInferred {
    bool old_value;
    ScopedRegionsInferred();
    ~ScopedRegionsInferred();
    ScopedRegionsInferred(const ScopedRegionsInferred &) = delete;
};

/** Simplify expressions found in a statement, but don't simplify
 * across different statements. This is safe to perform at an earlier
 * stage in lowering than full simplification of a stmt. */
Stmt simplify_exprs(const Stmt &);

}  // namespace Internal
}  // namespace Halide

#endif
