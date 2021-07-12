#ifndef HALIDE_CONSTANT_BOUNDS
#define HALIDE_CONSTANT_BOUNDS

#include "Expr.h"
#include "Interval.h"
#include "Scope.h"

/** \file
 * Defines a simplification pass for handling constant bounds
 */

namespace Halide {
namespace Internal {

bool possibly_correlated(const Expr &expr);

Expr substitute_some_lets(const Expr &expr, size_t count = 100);

Expr reorder_terms(const Expr &expr);

void print_relevant_scope(const Expr &expr, const Scope<Interval> &scope, std::ostream &stream);

Interval get_division_interval(const Expr &expr);

Interval try_constant_bounds_methods(const Expr &expr, const Scope<Interval> &scope);

std::ostream &operator<<(std::ostream &stream, const Interval &i);

}  // namespace Internal
}  // namespace Halide

#endif // HALIDE_CONSTANT_BOUNDS
