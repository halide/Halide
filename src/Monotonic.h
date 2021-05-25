#ifndef HALIDE_MONOTONIC_H
#define HALIDE_MONOTONIC_H

/** \file
 *
 * Methods for computing whether expressions are monotonic
 */
#include <iostream>
#include <string>

#include "Interval.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Find the bounds of the derivative of an expression. */
ConstantInterval derivative_bounds(const Expr &e, const std::string &var,
                                   const Scope<ConstantInterval> &scope = Scope<ConstantInterval>::empty_scope());

/**
 * Detect whether an expression is monotonic increasing in a variable,
 * decreasing, or unknown.
 */
enum class Monotonic { Constant,
                       Increasing,
                       Decreasing,
                       Unknown };
Monotonic is_monotonic(const Expr &e, const std::string &var,
                       const Scope<ConstantInterval> &scope = Scope<ConstantInterval>::empty_scope());
Monotonic is_monotonic(const Expr &e, const std::string &var, const Scope<Monotonic> &scope);

/** Emit the monotonic class in human-readable form for debugging. */
std::ostream &operator<<(std::ostream &stream, const Monotonic &m);

void is_monotonic_test();

}  // namespace Internal
}  // namespace Halide

#endif
