#ifndef HALIDE_CONSTANT_BOUNDS_H
#define HALIDE_CONSTANT_BOUNDS_H

#include "Bounds.h"
#include "ConstantInterval.h"
#include "Expr.h"
#include "Scope.h"

/** \file
 * Methods for computing compile-time constant int64_t upper and lower bounds of
 * an expression. Cheaper than symbolic bounds inference, and useful for things
 * like instruction selection.
 */

namespace Halide {
namespace Internal {

/** Deduce constant integer bounds on an expression. This can be useful to
 * decide if, for example, the expression can be cast to another type, be
 * negated, be incremented, etc without risking overflow.
 *
 * Also optionally accepts a scope containing the integer bounds of any
 * variables that may be referenced, a cache of constant integer bounds on
 * known Exprs, which this function will update, and previously-computed
 * FuncValueBounds for any Halide Call nodes encountered, which lets a call to
 * a producer Func (e.g. one known to be the result of a clamp) get a tighter
 * bound than its type's full range. The cache is helpful to short-circuit
 * large numbers of redundant queries, but it should not be used in contexts
 * where the same Expr object may take on different values within a single
 * Expr (i.e. before uniquify_variable_names).
 */
ConstantInterval constant_integer_bounds(const Expr &e,
                                         const Scope<ConstantInterval> &scope = Scope<ConstantInterval>::empty_scope(),
                                         std::map<Expr, ConstantInterval, ExprCompare> *cache = nullptr,
                                         const FuncValueBounds *func_bounds = nullptr);

void constant_bounds_test();

}  // namespace Internal
}  // namespace Halide

#endif
