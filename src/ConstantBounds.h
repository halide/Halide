#ifndef HALIDE_CONSTANT_BOUNDS_H
#define HALIDE_CONSTANT_BOUNDS_H

/** \file
 * Methods for detecting constant bounds on an expression.
 */

#include "Bounds.h"
#include "Expr.h"
#include "Interval.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Given a varying expression, try to find a constant that is either:
 * An upper bound (always greater than or equal to the expression), or
 * A lower bound (always less than or equal to the expression)
 * If it fails, returns an undefined Expr. */
enum class Direction { Upper,
                       Lower };
Expr find_constant_bound(const Expr &e, Direction d,
                         const Scope<Interval> &scope = Scope<Interval>::empty_scope());

/** Find bounds for a varying expression that are either constants or
 * +/-inf. */
Interval find_constant_bounds(const Expr &e, const Scope<Interval> &scope);

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_CONSTANT_BOUNDS_H
