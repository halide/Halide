#ifndef HALIDE_MONOTONIC_H
#define HALIDE_MONOTONIC_H

/** \file
 *
 * Methods for computing whether expressions are monotonic
 */
#include <iostream>
#include <string>

#include "Scope.h"
#include "Interval.h"

namespace Halide {
namespace Internal {

/** Find the bounds of the derivative of an expression. */
Interval derivative_bounds(const Expr &e, const std::string &var,
                           const Scope<Interval> &scope = Scope<Interval>::empty_scope(),
                           bool strong = false);

/**
 * Detect whether an expression is monotonic increasing in a variable,
 * decreasing, or unknown. If the scope is not empty, this adds some
 * overhead (and loses some capability to determine monotonicity) to
 * derivative_bounds above.
 * The `strong` parameter indicates whether the monotonicity analysis
 * will attempt to find monotonic relationships across correlated
 * expressions. This can be very expensive for large expressions.
 */
enum class Monotonic { Constant,
                       Increasing,
                       Decreasing,
                       Unknown };
Monotonic is_monotonic(const Expr &e, const std::string &var,
                       const Scope<Interval> &scope = Scope<Interval>::empty_scope(), bool strong = false);
Monotonic is_monotonic(const Expr &e, const std::string &var, const Scope<Monotonic> &scope, bool strong = false);
Monotonic is_monotonic_strong(const Expr &e, const std::string &var);

/** Emit the monotonic class in human-readable form for debugging. */
std::ostream &operator<<(std::ostream &stream, const Monotonic &m);

void is_monotonic_test();

}  // namespace Internal
}  // namespace Halide

#endif
