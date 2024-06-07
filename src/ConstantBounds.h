#ifndef HALIDE_CONSTANT_BOUNDS_H
#define HALIDE_CONSTANT_BOUNDS_H

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
 * variables that may be referenced, and a cache of constant integer bounds on
 * known Exprs, which this function will update. The cache is helpful to
 * short-circuit large numbers of redundant queries, but it should not be used
 * in contexts where the same Expr object may take on different values within a
 * single Expr (i.e. before uniquify_variable_names).
 */
ConstantInterval constant_integer_bounds(const Expr &e,
                                         const Scope<ConstantInterval> &scope = Scope<ConstantInterval>::empty_scope(),
                                         std::map<Expr, ConstantInterval, ExprCompare> *cache = nullptr);

}  // namespace Internal
}  // namespace Halide

#endif
