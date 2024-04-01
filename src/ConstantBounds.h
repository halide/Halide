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

// TODO: comments
ConstantInterval constant_integer_bounds(const Expr &e,
                                         const Scope<ConstantInterval> &scope = Scope<ConstantInterval>::empty_scope(),
                                         std::map<Expr, ConstantInterval, ExprCompare> *cache = nullptr);

}  // namespace Internal
}  // namespace Halide

#endif
