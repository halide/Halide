#ifndef HALIDE_MONOTONIC_H
#define HALIDE_MONOTONIC_H

/** \file
 *
 * Methods for computing whether expressions are monotonic
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/**
 * Detect whether an expression is monotonic increasing in a variable,
 * decreasing, or unknown.
 */
enum class Monotonic {Constant, Increasing, Decreasing, Unknown};
EXPORT Monotonic is_monotonic(Expr e, const std::string &var);

EXPORT void is_monotonic_test();

}
}

#endif
