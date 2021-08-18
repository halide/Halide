#ifndef HALIDE_APPROX_DIFFERENCES_H
#define HALIDE_APPROX_DIFFERENCES_H

/** \file
 * Approximation methods for cancelling differences to detect constant bounds.
 */

#include "Bounds.h"
#include "Expr.h"
#include "Interval.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

Expr push_rationals(const Expr &expr, Direction direction);

Expr strip_unbounded_terms(const Expr &expr, Direction direction, const Scope<Interval> &scope);

Expr reorder_terms(const Expr &expr);

Expr substitute_some_lets(const Expr &expr, size_t count = 100);

Expr approximate_constant_bound(const Expr &expr, Direction direction, const Scope<Interval> &scope);

Interval approximate_constant_bounds(const Expr &expr, const Scope<Interval> &scope);

}  // namespace Internal
}  // namespace Halide

#endif // HALIDE_APPROX_DIFFERENCES_H
