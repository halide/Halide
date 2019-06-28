#ifndef HALIDE_MONOTONIC_H
#define HALIDE_MONOTONIC_H

/** \file
 *
 * Methods for computing whether expressions are monotonic
 */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/**
 * Detect whether an expression is monotonic increasing in a variable,
 * decreasing, or unknown.
 */
enum class Monotonic {Constant, Increasing, Decreasing, Unknown};
Monotonic is_monotonic(Expr e, const std::string &var,
                       const Scope<Monotonic> &scope = Scope<Monotonic>::empty_scope());

void is_monotonic_test();

}  // namespace Internal
}  // namespace Halide

#endif
