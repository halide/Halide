#ifndef HALIDE_SUPER_SIMPLIFY_H
#define HALIDE_SUPER_SIMPLIFY_H

/** \file
 * Methods for simplifying halide statements and expressions
 */

#include "Expr.h"
#include "Interval.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

Expr super_simplify(const Expr &expr);


}  // namespace Internal
}  // namespace Halide

#endif // HALIDE_SUPER_SIMPLIFY_H
