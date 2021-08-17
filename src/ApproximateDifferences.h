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

}  // namespace Internal
}  // namespace Halide

#endif // HALIDE_APPROX_DIFFERENCES_H
